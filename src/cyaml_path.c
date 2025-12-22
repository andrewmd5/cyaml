#include "cyaml_internal.h"
#include <math.h>

// #region Limits

#define YPATH_MAX_STEPS 64
#define YPATH_MAX_DEPTH 16

// #endregion

// #region Token Types

typedef enum {
    TOK_EOF = 0,
    TOK_SLASH,
    TOK_DOT,
    TOK_DOTDOT,
    TOK_STAR,
    TOK_STARSTAR,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COLON,
    TOK_QUESTION,
    TOK_AT,
    TOK_OR,
    TOK_AND,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_PLUS,
    TOK_MINUS,
    TOK_DIV,
    TOK_BANG,
    TOK_IDENT,
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NULL,
    TOK_ERROR
} tok_t;

typedef struct {
    tok_t type;
    const char* start;
    uint32_t len;
    union {
        int64_t i;
        double f;
    } val;
} path_token_t;

// #endregion

// #region Lexer

typedef struct {
    const char* src;
    const char* cur;
    const char* end;
    path_token_t tok;
    const char* error;
    bool in_filter;
} lexer_t;

#define LEX_AT(l) (*(l)->cur)
#define LEX_PEEK(l, n) ((l)->cur + (n) < (l)->end ? (l)->cur[n] : C_NUL)
#define LEX_LEFT(l) ((uint32_t)((l)->end - (l)->cur))
#define LEX_ADV(l) ((l)->cur++)

#define PARSE_SIGNED_INT(p, out, flag, flags, on_err) \
    do {                                              \
        if ((p)->lex.tok.type == TOK_INT) {           \
            (out) = (p)->lex.tok.val.i;               \
            (flags) |= (flag);                        \
            lex_next(&(p)->lex);                      \
        } else if ((p)->lex.tok.type == TOK_MINUS) {  \
            lex_next(&(p)->lex);                      \
            if ((p)->lex.tok.type != TOK_INT) {       \
                parse_err(p, "expected integer");     \
                on_err;                               \
            }                                         \
            (out) = -(p)->lex.tok.val.i;              \
            (flags) |= (flag);                        \
            lex_next(&(p)->lex);                      \
        }                                             \
    } while (0)

static void lex_init(lexer_t* l, const char* path)
{
    l->src = l->cur = path;
    l->end = path + strlen(path);
    l->error = NULL;
    l->in_filter = false;
    l->tok.type = TOK_EOF;
}

inline static void lex_skip_ws(lexer_t* l)
{
    while (l->cur < l->end && CYAML_IS_WHITE(*l->cur))
        l->cur++;
}

#define IS_IDENT_START(c) (CYAML_IS_ALPHA(c) || (c) == '_')
#define IS_IDENT_CHAR(c) (IS_IDENT_START(c) || CYAML_IS_DIGIT(c) || (c) == '-')

static void lex_ident(lexer_t* l)
{
    l->tok.start = l->cur;
    while (l->cur < l->end && IS_IDENT_CHAR(*l->cur))
        l->cur++;
    l->tok.len = (uint32_t)(l->cur - l->tok.start);
    l->tok.type = TOK_IDENT;

    if (l->tok.len == L_TRUE && memcmp(l->tok.start, S_TRUE, L_TRUE) == 0)
        l->tok.type = TOK_TRUE;
    else if (l->tok.len == L_FALSE && memcmp(l->tok.start, S_FALSE, L_FALSE) == 0)
        l->tok.type = TOK_FALSE;
    else if (l->tok.len == L_NULL && memcmp(l->tok.start, S_NULL, L_NULL) == 0)
        l->tok.type = TOK_NULL;
}

static void lex_number(lexer_t* l)
{
    l->tok.start = l->cur;
    bool neg = (*l->cur == '-');
    if (neg)
        l->cur++;

    bool overflow = false;
    uint64_t uval = cyaml_parse_u64_n(&l->cur, l->end, &overflow);

    if (l->cur < l->end && (*l->cur == '.' || *l->cur == 'e' || *l->cur == 'E')) {
        double fval = (double)uval;
        if (*l->cur == '.') {
            l->cur++;
            double frac = 0.1;
            while (l->cur < l->end && CYAML_IS_DIGIT(*l->cur)) {
                fval += (*l->cur++ - '0') * frac;
                frac *= 0.1;
            }
        }
        if (l->cur < l->end && (*l->cur == 'e' || *l->cur == 'E')) {
            l->cur++;
            int exp_sign = 1;
            if (l->cur < l->end && (*l->cur == '+' || *l->cur == '-'))
                exp_sign = (*l->cur++ == '-') ? -1 : 1;
            bool exp_overflow = false;
            uint64_t exp_val = cyaml_parse_u64_n(&l->cur, l->end, &exp_overflow);
            if (exp_overflow || exp_val > 308) {
                l->tok.type = TOK_ERROR;
                l->error = "exponent overflow";
                return;
            }
            fval *= pow(10.0, exp_sign * (int)exp_val);
        }
        l->tok.type = TOK_FLOAT;
        l->tok.val.f = neg ? -fval : fval;
    } else {
        l->tok.type = TOK_INT;
        if (overflow) {
            l->tok.val.i = neg ? INT64_MIN : INT64_MAX;
        } else if (neg) {
            l->tok.val.i = (uval > (uint64_t)INT64_MAX + 1) ? INT64_MIN : -(int64_t)uval;
        } else {
            l->tok.val.i = (uval > (uint64_t)INT64_MAX) ? INT64_MAX : (int64_t)uval;
        }
    }
    l->tok.len = (uint32_t)(l->cur - l->tok.start);
}

static void lex_string(lexer_t* l, char q)
{
    l->cur++;
    l->tok.start = l->cur;
    while (l->cur < l->end && *l->cur != q) {
        if (*l->cur == C_BSLASH && l->cur + 1 < l->end)
            l->cur += 2;
        else if (q == '\'' && *l->cur == '\'' && LEX_PEEK(l, 1) == '\'')
            l->cur += 2;
        else
            l->cur++;
    }
    l->tok.len = (uint32_t)(l->cur - l->tok.start);
    l->tok.type = TOK_STRING;
    if (l->cur < l->end)
        l->cur++;
}

#define LEX_SINGLE(l, t)     \
    do {                     \
        (l)->cur++;          \
        (l)->tok.type = (t); \
    } while (0)
#define LEX_DOUBLE(l, c2, t1, t2)                       \
    do {                                                \
        (l)->cur++;                                     \
        if ((l)->cur < (l)->end && *(l)->cur == (c2)) { \
            (l)->cur++;                                 \
            (l)->tok.type = (t2);                       \
        } else                                          \
            (l)->tok.type = (t1);                       \
    } while (0)
#define LEX_REQUIRE(l, c2, t, err)                      \
    do {                                                \
        (l)->cur++;                                     \
        if ((l)->cur < (l)->end && *(l)->cur == (c2)) { \
            (l)->cur++;                                 \
            (l)->tok.type = (t);                        \
        } else {                                        \
            (l)->tok.type = TOK_ERROR;                  \
            (l)->error = (err);                         \
        }                                               \
    } while (0)

static void lex_next(lexer_t* l)
{
    lex_skip_ws(l);
    if (l->cur >= l->end) {
        l->tok = (path_token_t) { TOK_EOF, l->cur, 0, { 0 } };
        return;
    }

    char c = *l->cur;
    l->tok.start = l->cur;

    switch (c) {
    case '/':
        LEX_SINGLE(l, l->in_filter ? TOK_DIV : TOK_SLASH);
        break;
    case '.':
        LEX_DOUBLE(l, '.', TOK_DOT, TOK_DOTDOT);
        break;
    case '*':
        LEX_DOUBLE(l, '*', TOK_STAR, TOK_STARSTAR);
        break;
    case '[':
        LEX_SINGLE(l, TOK_LBRACKET);
        break;
    case ']':
        LEX_SINGLE(l, TOK_RBRACKET);
        break;
    case '(':
        LEX_SINGLE(l, TOK_LPAREN);
        break;
    case ')':
        LEX_SINGLE(l, TOK_RPAREN);
        break;
    case ':':
        LEX_SINGLE(l, TOK_COLON);
        break;
    case '?':
        LEX_SINGLE(l, TOK_QUESTION);
        break;
    case '@':
        LEX_SINGLE(l, TOK_AT);
        break;
    case '+':
        LEX_SINGLE(l, TOK_PLUS);
        break;
    case '|':
        LEX_REQUIRE(l, '|', TOK_OR, "expected ||");
        break;
    case '&':
        LEX_REQUIRE(l, '&', TOK_AND, "expected &&");
        break;
    case '=':
        LEX_REQUIRE(l, '=', TOK_EQ, "expected ==");
        break;
    case '!':
        LEX_DOUBLE(l, '=', TOK_BANG, TOK_NE);
        break;
    case '<':
        LEX_DOUBLE(l, '=', TOK_LT, TOK_LE);
        break;
    case '>':
        LEX_DOUBLE(l, '=', TOK_GT, TOK_GE);
        break;
    case '-':
        if (CYAML_IS_DIGIT(LEX_PEEK(l, 1)))
            lex_number(l);
        else
            LEX_SINGLE(l, TOK_MINUS);
        break;
    case '"':
    case '\'':
        lex_string(l, c);
        return;
    default:
        if (CYAML_IS_DIGIT(c)) {
            lex_number(l);
            return;
        }
        if (IS_IDENT_START(c)) {
            lex_ident(l);
            return;
        }
        l->cur++;
        l->tok.type = TOK_ERROR;
        l->error = "unexpected character";
        break;
    }
    l->tok.len = (uint32_t)(l->cur - l->tok.start);
}

// #endregion

// #region AST Types

typedef enum {
    STEP_IDENTITY,
    STEP_PARENT,
    STEP_WILDCARD,
    STEP_RECURSIVE,
    STEP_NAME,
    STEP_ALIAS,
    STEP_INDEX,
    STEP_SLICE,
    STEP_FILTER
} step_type_t;

typedef enum {
    EXPR_INT,
    EXPR_FLOAT,
    EXPR_STRING,
    EXPR_BOOL,
    EXPR_NULL,
    EXPR_PATH,
    EXPR_UNARY,
    EXPR_BINARY
} expr_type_t;

typedef enum {
    OP_OR,
    OP_AND,
    OP_EQ,
    OP_NE,
    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_NEG,
    OP_NOT
} op_t;

typedef struct expr expr_t;
typedef struct step step_t;

struct step {
    step_type_t type;
    union {
        struct {
            const char* s;
            uint32_t len;
        } name;
        int64_t idx;
        struct {
            int64_t start, end, step;
            uint8_t flags;
        } slice; // flags: 1=has_start, 2=has_end, 4=has_step
        expr_t* filter;
    } v;
};

struct expr {
    expr_type_t type;
    union {
        int64_t i;
        double f;
        struct {
            const char* s;
            uint32_t len;
        } str;
        bool b;
        struct {
            step_t* steps;
            uint32_t count;
        } path;
        struct {
            op_t op;
            expr_t* arg;
        } unary;
        struct {
            op_t op;
            expr_t *left, *right;
        } binary;
    } v;
};

typedef struct {
    step_t steps[YPATH_MAX_STEPS];
    uint32_t count;
    bool absolute;
} path_t;

// #endregion

// #region Expression Pool

typedef struct {
    expr_t exprs[128];
    step_t steps[256];
    uint32_t expr_count;
    uint32_t step_count;
} expr_pool_t;

static inline expr_t* pool_expr(expr_pool_t* p, expr_type_t type)
{
    if (p->expr_count >= 128)
        return NULL;
    expr_t* e = &p->exprs[p->expr_count++];
    memset(e, 0, sizeof(*e));
    e->type = type;
    return e;
}

static inline step_t* pool_step(expr_pool_t* p)
{
    if (p->step_count >= 256)
        return NULL;
    step_t* s = &p->steps[p->step_count++];
    memset(s, 0, sizeof(*s));
    return s;
}

// #endregion

// #region Parser

typedef struct {
    lexer_t lex;
    expr_pool_t* pool;
    const char* error;
    uint32_t error_pos;
} path_parser_t;

static inline void parse_err(path_parser_t* p, const char* msg)
{
    if (!p->error) {
        p->error = msg;
        p->error_pos = (uint32_t)(p->lex.tok.start - p->lex.src);
    }
}

static bool expect(path_parser_t* p, tok_t t)
{
    if (p->lex.tok.type == t) {
        lex_next(&p->lex);
        return true;
    }
    parse_err(p, "unexpected token");
    return false;
}

static expr_t* parse_expr(path_parser_t* p);

static step_t* add_step(path_parser_t* p, path_t* path)
{
    if (path->count >= YPATH_MAX_STEPS) {
        parse_err(p, "too many steps");
        return NULL;
    }
    step_t* s = &path->steps[path->count++];
    memset(s, 0, sizeof(*s));
    return s;
}

static bool parse_bracket(path_parser_t* p, path_t* path)
{
    lex_next(&p->lex);

    if (p->lex.tok.type == TOK_QUESTION) {
        lex_next(&p->lex);
        p->lex.in_filter = true;
        expr_t* f = parse_expr(p);
        p->lex.in_filter = false;
        if (!f)
            return false;
        step_t* s = add_step(p, path);
        if (!s)
            return false;
        s->type = STEP_FILTER;
        s->v.filter = f;
    } else if (p->lex.tok.type == TOK_INT || p->lex.tok.type == TOK_COLON || p->lex.tok.type == TOK_MINUS) {
        int64_t start = 0, end = 0, step_val = 1;
        uint8_t flags = 0;
        bool is_slice = false;

        PARSE_SIGNED_INT(p, start, 1, flags, return false);

        if (p->lex.tok.type == TOK_COLON) {
            is_slice = true;
            lex_next(&p->lex);
            PARSE_SIGNED_INT(p, end, 2, flags, return false);
            if (p->lex.tok.type == TOK_COLON) {
                lex_next(&p->lex);
                PARSE_SIGNED_INT(p, step_val, 4, flags, return false);
            }
        }

        step_t* s = add_step(p, path);
        if (!s)
            return false;
        if (is_slice) {
            s->type = STEP_SLICE;
            s->v.slice.start = start;
            s->v.slice.end = end;
            s->v.slice.step = step_val;
            s->v.slice.flags = flags;
        } else {
            s->type = STEP_INDEX;
            s->v.idx = start;
        }
    } else if (p->lex.tok.type == TOK_STAR) {
        lex_next(&p->lex);
        step_t* s = add_step(p, path);
        if (!s)
            return false;
        s->type = STEP_WILDCARD;
    } else {
        parse_err(p, "expected index, slice, or filter");
        return false;
    }

    return expect(p, TOK_RBRACKET);
}

static bool parse_step(path_parser_t* p, path_t* path)
{
    step_t* s;
    switch (p->lex.tok.type) {
    case TOK_DOT:
        lex_next(&p->lex);
        if (!(s = add_step(p, path)))
            return false;
        s->type = STEP_IDENTITY;
        break;
    case TOK_DOTDOT:
        lex_next(&p->lex);
        if (!(s = add_step(p, path)))
            return false;
        s->type = STEP_PARENT;
        break;
    case TOK_STARSTAR:
        lex_next(&p->lex);
        if (!(s = add_step(p, path)))
            return false;
        s->type = STEP_RECURSIVE;
        break;
    case TOK_STAR:
        lex_next(&p->lex);
        if (p->lex.tok.type == TOK_IDENT) {
            if (!(s = add_step(p, path)))
                return false;
            s->type = STEP_ALIAS;
            s->v.name.s = p->lex.tok.start;
            s->v.name.len = p->lex.tok.len;
            lex_next(&p->lex);
        } else {
            if (!(s = add_step(p, path)))
                return false;
            s->type = STEP_WILDCARD;
        }
        break;
    case TOK_IDENT:
    case TOK_STRING:
    case TOK_INT:
        if (!(s = add_step(p, path)))
            return false;
        s->type = STEP_NAME;
        s->v.name.s = p->lex.tok.start;
        s->v.name.len = p->lex.tok.len;
        lex_next(&p->lex);
        break;
    case TOK_LBRACKET:
        if (!parse_bracket(p, path))
            return false;
        break;
    default:
        parse_err(p, "expected step");
        return false;
    }

    while (p->lex.tok.type == TOK_LBRACKET)
        if (!parse_bracket(p, path))
            return false;
    return true;
}

static bool parse_path_steps(path_parser_t* p, path_t* path)
{
    if (p->lex.tok.type == TOK_EOF || p->lex.tok.type == TOK_RPAREN || p->lex.tok.type == TOK_RBRACKET)
        return true;
    if (!parse_step(p, path))
        return false;
    while (p->lex.tok.type == TOK_SLASH) {
        lex_next(&p->lex);
        if (!parse_step(p, path))
            return false;
    }
    return true;
}

static expr_t* parse_primary(path_parser_t* p)
{
    expr_t* e;
    switch (p->lex.tok.type) {
    case TOK_INT:
        if (!(e = pool_expr(p->pool, EXPR_INT)))
            return NULL;
        e->v.i = p->lex.tok.val.i;
        lex_next(&p->lex);
        return e;
    case TOK_FLOAT:
        if (!(e = pool_expr(p->pool, EXPR_FLOAT)))
            return NULL;
        e->v.f = p->lex.tok.val.f;
        lex_next(&p->lex);
        return e;
    case TOK_STRING:
        if (!(e = pool_expr(p->pool, EXPR_STRING)))
            return NULL;
        e->v.str.s = p->lex.tok.start;
        e->v.str.len = p->lex.tok.len;
        lex_next(&p->lex);
        return e;
    case TOK_TRUE:
    case TOK_FALSE:
        if (!(e = pool_expr(p->pool, EXPR_BOOL)))
            return NULL;
        e->v.b = (p->lex.tok.type == TOK_TRUE);
        lex_next(&p->lex);
        return e;
    case TOK_NULL:
        if (!(e = pool_expr(p->pool, EXPR_NULL)))
            return NULL;
        lex_next(&p->lex);
        return e;
    case TOK_AT: {
        lex_next(&p->lex);
        if (!(e = pool_expr(p->pool, EXPR_PATH)))
            return NULL;
        step_t* first = &p->pool->steps[p->pool->step_count];
        uint32_t start_count = p->pool->step_count;
        while (p->lex.tok.type == TOK_SLASH || p->lex.tok.type == TOK_DOT || p->lex.tok.type == TOK_LBRACKET) {
            if (p->lex.tok.type == TOK_SLASH || p->lex.tok.type == TOK_DOT)
                lex_next(&p->lex);
            step_t* s = pool_step(p->pool);
            if (!s)
                return NULL;
            switch (p->lex.tok.type) {
            case TOK_IDENT:
            case TOK_STRING:
                s->type = STEP_NAME;
                s->v.name.s = p->lex.tok.start;
                s->v.name.len = p->lex.tok.len;
                lex_next(&p->lex);
                break;
            case TOK_LBRACKET:
                p->pool->step_count--;
                path_t tmp = { .count = 0 };
                if (!parse_bracket(p, &tmp))
                    return NULL;
                memcpy(s, &tmp.steps[0], sizeof(step_t));
                p->pool->step_count++;
                break;
            default:
                parse_err(p, "expected path step");
                return NULL;
            }
        }
        e->v.path.steps = first;
        e->v.path.count = p->pool->step_count - start_count;
        return e;
    }
    case TOK_LPAREN:
        lex_next(&p->lex);
        e = parse_expr(p);
        if (!e)
            return NULL;
        if (!expect(p, TOK_RPAREN))
            return NULL;
        return e;
    default:
        parse_err(p, "expected expression");
        return NULL;
    }
}

// Operator precedence (higher = binds tighter)
static int op_prec(tok_t t)
{
    switch (t) {
    case TOK_OR:
        return 1;
    case TOK_AND:
        return 2;
    case TOK_EQ:
    case TOK_NE:
        return 3;
    case TOK_LT:
    case TOK_LE:
    case TOK_GT:
    case TOK_GE:
        return 4;
    case TOK_PLUS:
    case TOK_MINUS:
        return 5;
    case TOK_STAR:
    case TOK_DIV:
        return 6;
    default:
        return 0;
    }
}

static op_t tok_to_op(tok_t t)
{
    switch (t) {
    case TOK_OR:
        return OP_OR;
    case TOK_AND:
        return OP_AND;
    case TOK_EQ:
        return OP_EQ;
    case TOK_NE:
        return OP_NE;
    case TOK_LT:
        return OP_LT;
    case TOK_LE:
        return OP_LE;
    case TOK_GT:
        return OP_GT;
    case TOK_GE:
        return OP_GE;
    case TOK_PLUS:
        return OP_ADD;
    case TOK_MINUS:
        return OP_SUB;
    case TOK_STAR:
        return OP_MUL;
    case TOK_DIV:
        return OP_DIV;
    default:
        return (op_t)-1;
    }
}

#define EXPR_STACK_CAP 64

typedef struct {
    op_t op;
    int prec;
    bool unary;
} op_entry_t;

typedef struct {
    expr_t* operands[EXPR_STACK_CAP];
    op_entry_t ops[EXPR_STACK_CAP];
    int operand_count;
    int op_count;
} expr_stack_t;

static bool expr_stack_reduce(path_parser_t* p, expr_stack_t* s)
{
    if (s->op_count == 0)
        return false;
    s->op_count--;
    if (s->ops[s->op_count].unary) {
        if (s->operand_count < 1)
            return false;
        expr_t* arg = s->operands[--s->operand_count];
        expr_t* e = pool_expr(p->pool, EXPR_UNARY);
        if (!e)
            return false;
        e->v.unary.op = s->ops[s->op_count].op;
        e->v.unary.arg = arg;
        s->operands[s->operand_count++] = e;
    } else {
        if (s->operand_count < 2)
            return false;
        expr_t* right = s->operands[--s->operand_count];
        expr_t* left = s->operands[--s->operand_count];
        expr_t* e = pool_expr(p->pool, EXPR_BINARY);
        if (!e)
            return false;
        e->v.binary.op = s->ops[s->op_count].op;
        e->v.binary.left = left;
        e->v.binary.right = right;
        s->operands[s->operand_count++] = e;
    }
    return true;
}

static expr_t* parse_expr(path_parser_t* p)
{
    expr_stack_t s = { .operand_count = 0, .op_count = 0 };
    bool expect_operand = true;

    for (;;) {
        if (expect_operand) {
            // Handle prefix unary operators
            while (p->lex.tok.type == TOK_MINUS || p->lex.tok.type == TOK_BANG) {
                if (s.op_count >= EXPR_STACK_CAP) {
                    parse_err(p, "expression too complex");
                    return NULL;
                }
                op_t op = (p->lex.tok.type == TOK_MINUS) ? OP_NEG : OP_NOT;
                s.ops[s.op_count++] = (op_entry_t) { op, 100, true };
                lex_next(&p->lex);
            }

            // Parse primary
            expr_t* e = parse_primary(p);
            if (!e)
                return NULL;
            if (s.operand_count >= EXPR_STACK_CAP) {
                parse_err(p, "expression too complex");
                return NULL;
            }
            s.operands[s.operand_count++] = e;

            // Reduce any pending unary operators
            while (s.op_count > 0 && s.ops[s.op_count - 1].unary) {
                if (!expr_stack_reduce(p, &s))
                    return NULL;
            }
            expect_operand = false;
        } else {
            // Check for binary operator
            int prec = op_prec(p->lex.tok.type);
            if (prec == 0)
                break;

            // Reduce operators with higher or equal precedence (left associative)
            while (s.op_count > 0 && !s.ops[s.op_count - 1].unary && s.ops[s.op_count - 1].prec >= prec) {
                if (!expr_stack_reduce(p, &s))
                    return NULL;
            }

            if (s.op_count >= EXPR_STACK_CAP) {
                parse_err(p, "expression too complex");
                return NULL;
            }
            s.ops[s.op_count++] = (op_entry_t) { tok_to_op(p->lex.tok.type), prec, false };
            lex_next(&p->lex);
            expect_operand = true;
        }
    }

    // Reduce remaining operators
    while (s.op_count > 0) {
        if (!expr_stack_reduce(p, &s))
            return NULL;
    }

    if (s.operand_count != 1) {
        parse_err(p, "invalid expression");
        return NULL;
    }
    return s.operands[0];
}

static bool parse_path(path_parser_t* p, path_t* path)
{
    memset(path, 0, sizeof(*path));
    if (p->lex.tok.type == TOK_SLASH) {
        path->absolute = true;
        lex_next(&p->lex);
    }
    return parse_path_steps(p, path);
}

// #endregion

// #region Evaluator

#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) \
                                                         : (v))

static inline int64_t norm_slice_idx(int64_t idx, int64_t len)
{
    idx = CLAMP(idx, -len, len);
    return idx < 0 ? idx + len : idx;
}

typedef struct {
    const cyaml_doc_t* doc;
    const cyaml_node_t* root;
    const cyaml_node_t* current;
    const char* src;
} eval_t;

typedef struct {
    cyaml_node_t** nodes;
    uint32_t count;
    uint32_t cap;
} nodebuf_t;

static inline void nodebuf_free(nodebuf_t* b) { free(b->nodes); }

static bool nodebuf_add(nodebuf_t* b, cyaml_node_t* n)
{
    for (uint32_t i = 0; i < b->count; i++)
        if (b->nodes[i] == n)
            return true;
    if (b->count >= b->cap) {
        uint32_t new_cap = b->cap ? b->cap * 2 : 16;
        cyaml_node_t** new_nodes = realloc(b->nodes, new_cap * sizeof(*new_nodes));
        if (!new_nodes)
            return false;
        b->nodes = new_nodes;
        b->cap = new_cap;
    }
    b->nodes[b->count++] = n;
    return true;
}

#define PATH_STACK_INIT_CAP 32

typedef struct {
    cyaml_node_t* node;
    uint32_t child_idx;
    uint8_t phase;
} path_frame_t;

#define STACK_INIT(stk, cnt, cap, on_fail)                          \
    path_frame_t* stk = malloc(PATH_STACK_INIT_CAP * sizeof(*stk)); \
    if (!stk) {                                                     \
        on_fail;                                                    \
    }                                                               \
    size_t cnt = 0, cap = PATH_STACK_INIT_CAP

#define STACK_PUSH(stk, cnt, cap, nd, on_fail)                            \
    do {                                                                  \
        if ((cnt) >= (cap)) {                                             \
            size_t new_cap = (cap) * 2;                                   \
            path_frame_t* tmp = realloc((stk), new_cap * sizeof(*(stk))); \
            if (!tmp) {                                                   \
                on_fail;                                                  \
            }                                                             \
            (stk) = tmp;                                                  \
            (cap) = new_cap;                                              \
        }                                                                 \
        (stk)[(cnt)] = (path_frame_t) { (nd), 0, 0 };                     \
        (cnt)++;                                                          \
    } while (0)

#define STACK_ITER_SEQ(f, cur, stk, cnt, cap, on_fail)        \
    if ((f)->child_idx >= (cur)->seq.count) {                 \
        (cnt)--;                                              \
    } else {                                                  \
        cyaml_node_t* c = (cur)->seq.items[(f)->child_idx++]; \
        if (c)                                                \
            STACK_PUSH(stk, cnt, cap, c, on_fail);            \
    }

#define STACK_ITER_MAP_VAL(f, cur, stk, cnt, cap, on_fail)        \
    if ((f)->child_idx >= (cur)->map.count) {                     \
        (cnt)--;                                                  \
    } else {                                                      \
        cyaml_node_t* c = (cur)->map.pairs[(f)->child_idx++].val; \
        if (c)                                                    \
            STACK_PUSH(stk, cnt, cap, c, on_fail);                \
    }

#define STACK_ITER_MAP_ALL(f, cur, stk, cnt, cap, on_fail)        \
    if ((f)->child_idx >= (cur)->map.count) {                     \
        (cnt)--;                                                  \
    } else {                                                      \
        cyaml_node_t* k = (cur)->map.pairs[(f)->child_idx].key;   \
        cyaml_node_t* v = (cur)->map.pairs[(f)->child_idx++].val; \
        if (k)                                                    \
            STACK_PUSH(stk, cnt, cap, k, on_fail);                \
        if (v)                                                    \
            STACK_PUSH(stk, cnt, cap, v, on_fail);                \
    }

static void collect_all(cyaml_node_t* n, nodebuf_t* b)
{
    if (!n)
        return;

    STACK_INIT(stack, stack_count, stack_cap, return);
    STACK_PUSH(stack, stack_count, stack_cap, n, { free(stack); return; });

    while (stack_count > 0) {
        path_frame_t* f = &stack[stack_count - 1];
        cyaml_node_t* cur = f->node;

        if (f->phase == 0) {
            nodebuf_add(b, cur);
            f->phase = 1;
        }

        if (cur->type == CYAML_SEQ)
            STACK_ITER_SEQ(f, cur, stack, stack_count, stack_cap, { free(stack); return; })
        else if (cur->type == CYAML_MAP)
            STACK_ITER_MAP_VAL(f, cur, stack, stack_count, stack_cap, { free(stack); return; })
        else
            stack_count--;
    }

    free(stack);
}

static cyaml_node_t* find_parent(const cyaml_node_t* root, const cyaml_node_t* child)
{
    if (!root || root == child)
        return NULL;

    STACK_INIT(stack, stack_count, stack_cap, return NULL);
    cyaml_node_t* result = NULL;
    STACK_PUSH(stack, stack_count, stack_cap, (cyaml_node_t*)root, { free(stack); return NULL; });

    while (stack_count > 0) {
        path_frame_t* f = &stack[stack_count - 1];
        cyaml_node_t* cur = f->node;

        if (cur->type == CYAML_SEQ) {
            if (f->child_idx >= cur->seq.count) {
                stack_count--;
            } else {
                cyaml_node_t* c = cur->seq.items[f->child_idx++];
                if (c == child) {
                    result = cur;
                    break;
                }
                if (c)
                    STACK_PUSH(stack, stack_count, stack_cap, c, { free(stack); return NULL; });
            }
        } else if (cur->type == CYAML_MAP) {
            if (f->child_idx >= cur->map.count) {
                stack_count--;
            } else {
                cyaml_node_t* k = cur->map.pairs[f->child_idx].key;
                cyaml_node_t* v = cur->map.pairs[f->child_idx++].val;
                if (k == child || v == child) {
                    result = cur;
                    break;
                }
                if (k)
                    STACK_PUSH(stack, stack_count, stack_cap, k, { free(stack); return NULL; });
                if (v)
                    STACK_PUSH(stack, stack_count, stack_cap, v, { free(stack); return NULL; });
            }
        } else {
            stack_count--;
        }
    }

    free(stack);
    return result;
}

static cyaml_node_t* find_anchor(const cyaml_node_t* n, const char* name, uint32_t len, const char* src)
{
    if (!n)
        return NULL;

    STACK_INIT(stack, stack_count, stack_cap, return NULL);
    cyaml_node_t* result = NULL;
    STACK_PUSH(stack, stack_count, stack_cap, (cyaml_node_t*)n, { free(stack); return NULL; });

    while (stack_count > 0) {
        path_frame_t* f = &stack[stack_count - 1];
        cyaml_node_t* cur = f->node;

        if (f->phase == 0) {
            if (cur->anchor.len == len && memcmp(src + cur->anchor.off, name, len) == 0) {
                result = cur;
                break;
            }
            f->phase = 1;
        }

        if (cur->type == CYAML_SEQ)
            STACK_ITER_SEQ(f, cur, stack, stack_count, stack_cap, { free(stack); return NULL; })
        else if (cur->type == CYAML_MAP)
            STACK_ITER_MAP_ALL(f, cur, stack, stack_count, stack_cap, { free(stack); return NULL; })
        else
            stack_count--;
    }

    free(stack);
    return result;
}

static cyaml_node_t* path_resolve_alias(cyaml_node_t* n, const cyaml_node_t* root, const char* src)
{
    if (!n || n->type != CYAML_ALIAS)
        return n;
    if (n->alias.target)
        return n->alias.target;
    if (n->anchor.len == 0)
        return NULL;
    return find_anchor(root, src + n->anchor.off, n->anchor.len, src);
}

typedef struct {
    enum { VAL_NULL,
        VAL_BOOL,
        VAL_INT,
        VAL_FLOAT,
        VAL_STR,
        VAL_NODES } type;
    union {
        bool b;
        int64_t i;
        double f;
        struct {
            const char* s;
            uint32_t len;
        } str;
        nodebuf_t nodes;
    } v;
} val_t;

#define STR_EQ(s1, l1, s2, l2) ((l1) == (l2) && memcmp((s1), (s2), (l1)) == 0)

static bool str_truthy(const char* s, uint32_t len)
{
    if (len == 0)
        return false;
    if (STR_EQ(s, len, S_NULL, L_NULL) || STR_EQ(s, len, S_FALSE, L_FALSE) || STR_EQ(s, len, S_TILDE, L_TILDE) || STR_EQ(s, len, "0", 1))
        return false;
    return true;
}

static bool str_null(const char* s, uint32_t len)
{
    return len == 0 || STR_EQ(s, len, S_NULL, L_NULL) || STR_EQ(s, len, S_TILDE, L_TILDE);
}

static bool val_truthy(const val_t* v, const char* src)
{
    switch (v->type) {
    case VAL_NULL:
        return false;
    case VAL_BOOL:
        return v->v.b;
    case VAL_INT:
        return v->v.i != 0;
    case VAL_FLOAT:
        return v->v.f != 0.0;
    case VAL_STR:
        return str_truthy(v->v.str.s, v->v.str.len);
    case VAL_NODES:
        if (v->v.nodes.count == 1 && v->v.nodes.nodes[0]->type == CYAML_SCALAR) {
            cyaml_node_t* n = v->v.nodes.nodes[0];
            return str_truthy(src + n->span.off, n->span.len);
        }
        return v->v.nodes.count > 0;
    }
    return false;
}

static double val_float(const val_t* v, const char* src)
{
    double f;
    switch (v->type) {
    case VAL_INT:
        return (double)v->v.i;
    case VAL_FLOAT:
        return v->v.f;
    case VAL_STR: {
        char buf[64];
        uint32_t len = v->v.str.len < 63 ? v->v.str.len : 63;
        memcpy(buf, v->v.str.s, len);
        buf[len] = 0;
        return cyaml_str_to_f64(buf, NULL, &f) ? f : 0.0;
    }
    case VAL_NODES:
        if (v->v.nodes.count == 1 && v->v.nodes.nodes[0]->type == CYAML_SCALAR) {
            cyaml_node_t* n = v->v.nodes.nodes[0];
            char buf[64];
            uint32_t len = n->span.len < 63 ? n->span.len : 63;
            memcpy(buf, src + n->span.off, len);
            buf[len] = 0;
            return cyaml_str_to_f64(buf, NULL, &f) ? f : 0.0;
        }
        return 0.0;
    default:
        return 0.0;
    }
}

static bool val_eq(const val_t* a, const val_t* b, const char* src)
{
    val_t ta = *a, tb = *b;

    if (a->type == VAL_NODES && a->v.nodes.count == 1) {
        cyaml_node_t* n = a->v.nodes.nodes[0];
        if (n->type == CYAML_SCALAR) {
            ta.type = VAL_STR;
            ta.v.str.s = src + n->span.off;
            ta.v.str.len = n->span.len;
        } else if (n->type == CYAML_NULL)
            ta.type = VAL_NULL;
    }
    if (b->type == VAL_NODES && b->v.nodes.count == 1) {
        cyaml_node_t* n = b->v.nodes.nodes[0];
        if (n->type == CYAML_SCALAR) {
            tb.type = VAL_STR;
            tb.v.str.s = src + n->span.off;
            tb.v.str.len = n->span.len;
        } else if (n->type == CYAML_NULL)
            tb.type = VAL_NULL;
    }

    if (ta.type == VAL_NULL && tb.type == VAL_NULL)
        return true;
    if (ta.type == VAL_NULL && tb.type == VAL_STR)
        return str_null(tb.v.str.s, tb.v.str.len);
    if (tb.type == VAL_NULL && ta.type == VAL_STR)
        return str_null(ta.v.str.s, ta.v.str.len);
    if (ta.type == VAL_NULL || tb.type == VAL_NULL)
        return false;

    if (ta.type == VAL_STR && tb.type == VAL_STR)
        return STR_EQ(ta.v.str.s, ta.v.str.len, tb.v.str.s, tb.v.str.len);

    if ((ta.type == VAL_INT || ta.type == VAL_FLOAT) && (tb.type == VAL_INT || tb.type == VAL_FLOAT))
        return val_float(&ta, src) == val_float(&tb, src);

    if (ta.type == VAL_BOOL && tb.type == VAL_BOOL)
        return ta.v.b == tb.v.b;

    if (ta.type == VAL_STR && tb.type == VAL_BOOL) {
        if (STR_EQ(ta.v.str.s, ta.v.str.len, S_TRUE, L_TRUE))
            return tb.v.b;
        if (STR_EQ(ta.v.str.s, ta.v.str.len, S_FALSE, L_FALSE))
            return !tb.v.b;
        return false;
    }
    if (tb.type == VAL_STR && ta.type == VAL_BOOL) {
        if (STR_EQ(tb.v.str.s, tb.v.str.len, S_TRUE, L_TRUE))
            return ta.v.b;
        if (STR_EQ(tb.v.str.s, tb.v.str.len, S_FALSE, L_FALSE))
            return !ta.v.b;
        return false;
    }

    if ((ta.type == VAL_STR || tb.type == VAL_STR) && (ta.type == VAL_INT || ta.type == VAL_FLOAT || tb.type == VAL_INT || tb.type == VAL_FLOAT))
        return val_float(&ta, src) == val_float(&tb, src);

    return false;
}

static val_t eval_expr(eval_t* ctx, const expr_t* e);

static inline void val_free(val_t* v)
{
    if (v->type == VAL_NODES)
        nodebuf_free(&v->v.nodes);
}

static val_t eval_path_on(eval_t* ctx, cyaml_node_t* start, const step_t* steps, uint32_t count)
{
    val_t result = { .type = VAL_NODES };
    nodebuf_t in = { 0 }, out = { 0 };
    nodebuf_add(&in, start);

    for (uint32_t si = 0; si < count; si++) {
        const step_t* s = &steps[si];
        out.count = 0;

        for (uint32_t ni = 0; ni < in.count; ni++) {
            cyaml_node_t* n = path_resolve_alias(in.nodes[ni], ctx->root, ctx->src);
            if (!n)
                continue;

            switch (s->type) {
            case STEP_IDENTITY:
                nodebuf_add(&out, n);
                break;
            case STEP_PARENT: {
                cyaml_node_t* p = find_parent(ctx->root, n);
                if (p)
                    nodebuf_add(&out, p);
                break;
            }
            case STEP_WILDCARD:
                if (n->type == CYAML_SEQ)
                    for (uint32_t i = 0; i < n->seq.count; i++)
                        nodebuf_add(&out, n->seq.items[i]);
                else if (n->type == CYAML_MAP)
                    for (uint32_t i = 0; i < n->map.count; i++)
                        nodebuf_add(&out, n->map.pairs[i].val);
                break;
            case STEP_RECURSIVE:
                collect_all(n, &out);
                break;
            case STEP_NAME:
                if (n->type == CYAML_MAP && ctx->src)
                    for (uint32_t i = 0; i < n->map.count; i++) {
                        cyaml_node_t* k = n->map.pairs[i].key;
                        if (k && k->type == CYAML_SCALAR && STR_EQ(ctx->src + k->span.off, k->span.len, s->v.name.s, s->v.name.len)) {
                            nodebuf_add(&out, n->map.pairs[i].val);
                            break;
                        }
                    }
                break;
            case STEP_ALIAS:
                if (ctx->src) {
                    cyaml_node_t* f = find_anchor(ctx->root, s->v.name.s, s->v.name.len, ctx->src);
                    if (f)
                        nodebuf_add(&out, f);
                }
                break;
            case STEP_INDEX:
                if (n->type == CYAML_SEQ) {
                    int64_t idx = s->v.idx;
                    if (idx < 0)
                        idx += (int64_t)n->seq.count;
                    if (idx >= 0 && idx < (int64_t)n->seq.count)
                        nodebuf_add(&out, n->seq.items[idx]);
                }
                break;
            case STEP_SLICE:
                if (n->type == CYAML_SEQ) {
                    int64_t len = (int64_t)n->seq.count;
                    int64_t ss = (s->v.slice.flags & 1) ? s->v.slice.start : 0;
                    int64_t se = (s->v.slice.flags & 2) ? s->v.slice.end : len;
                    int64_t st = (s->v.slice.flags & 4) ? s->v.slice.step : 1;
                    if (st == 0)
                        st = 1;
                    if (st > 0) {
                        ss = norm_slice_idx(ss, len);
                        se = norm_slice_idx(se, len);
                        for (int64_t i = ss; i < se; i += st)
                            nodebuf_add(&out, n->seq.items[i]);
                    } else {
                        ss = (s->v.slice.flags & 1) ? CLAMP(s->v.slice.start, -len, len - 1) : len - 1;
                        se = (s->v.slice.flags & 2) ? CLAMP(s->v.slice.end, -len - 1, len) : -len - 1;
                        if (ss < 0)
                            ss += len;
                        if (se < -1)
                            se += len;
                        for (int64_t i = ss; i > se && i >= 0; i += st)
                            nodebuf_add(&out, n->seq.items[i]);
                    }
                }
                break;
            case STEP_FILTER: {
                eval_t fc = *ctx;
                if (n->type == CYAML_SEQ) {
                    for (uint32_t i = 0; i < n->seq.count; i++) {
                        fc.current = n->seq.items[i];
                        val_t v = eval_expr(&fc, s->v.filter);
                        if (val_truthy(&v, ctx->src))
                            nodebuf_add(&out, n->seq.items[i]);
                        val_free(&v);
                    }
                } else if (n->type == CYAML_MAP) {
                    for (uint32_t i = 0; i < n->map.count; i++) {
                        fc.current = n->map.pairs[i].val;
                        val_t v = eval_expr(&fc, s->v.filter);
                        if (val_truthy(&v, ctx->src))
                            nodebuf_add(&out, n->map.pairs[i].val);
                        val_free(&v);
                    }
                } else {
                    fc.current = n;
                    val_t v = eval_expr(&fc, s->v.filter);
                    if (val_truthy(&v, ctx->src))
                        nodebuf_add(&out, n);
                    val_free(&v);
                }
                break;
            }
            }
        }
        nodebuf_t tmp = in;
        in = out;
        out = tmp;
        out.count = 0;
    }
    nodebuf_free(&out);
    result.v.nodes = in;
    return result;
}

#define EVAL_STACK_CAP 64

typedef struct {
    const expr_t* expr;
    uint8_t phase;
} eval_frame_t;

static val_t eval_expr(eval_t* ctx, const expr_t* e)
{
    eval_frame_t stack[EVAL_STACK_CAP];
    val_t vals[EVAL_STACK_CAP];
    int sp = 0, vp = 0;

    stack[sp++] = (eval_frame_t) { e, 0 };

    while (sp > 0) {
        eval_frame_t* f = &stack[sp - 1];
        const expr_t* cur = f->expr;

        switch (cur->type) {
        case EXPR_INT:
            sp--;
            vals[vp++] = (val_t) { .type = VAL_INT, .v.i = cur->v.i };
            break;
        case EXPR_FLOAT:
            sp--;
            vals[vp++] = (val_t) { .type = VAL_FLOAT, .v.f = cur->v.f };
            break;
        case EXPR_STRING:
            sp--;
            vals[vp++] = (val_t) { .type = VAL_STR, .v.str = { cur->v.str.s, cur->v.str.len } };
            break;
        case EXPR_BOOL:
            sp--;
            vals[vp++] = (val_t) { .type = VAL_BOOL, .v.b = cur->v.b };
            break;
        case EXPR_NULL:
            sp--;
            vals[vp++] = (val_t) { .type = VAL_NULL };
            break;
        case EXPR_PATH:
            sp--;
            if (ctx->current)
                vals[vp++] = eval_path_on(ctx, (cyaml_node_t*)ctx->current, cur->v.path.steps, cur->v.path.count);
            else
                vals[vp++] = (val_t) { .type = VAL_NULL };
            break;
        case EXPR_UNARY:
            if (f->phase == 0) {
                f->phase = 1;
                if (sp >= EVAL_STACK_CAP)
                    goto overflow;
                stack[sp++] = (eval_frame_t) { cur->v.unary.arg, 0 };
            } else {
                sp--;
                val_t arg = vals[--vp];
                val_t r;
                if (cur->v.unary.op == OP_NEG) {
                    r.type = VAL_FLOAT;
                    r.v.f = -val_float(&arg, ctx->src);
                } else {
                    r.type = VAL_BOOL;
                    r.v.b = !val_truthy(&arg, ctx->src);
                }
                val_free(&arg);
                vals[vp++] = r;
            }
            break;
        case EXPR_BINARY:
            if (f->phase == 0) {
                f->phase = 1;
                if (sp >= EVAL_STACK_CAP)
                    goto overflow;
                stack[sp++] = (eval_frame_t) { cur->v.binary.left, 0 };
            } else if (f->phase == 1) {
                val_t left = vals[vp - 1];
                bool left_truthy = val_truthy(&left, ctx->src);
                if (cur->v.binary.op == OP_AND && !left_truthy) {
                    sp--;
                    vals[vp - 1] = (val_t) { .type = VAL_BOOL, .v.b = false };
                    val_free(&left);
                } else if (cur->v.binary.op == OP_OR && left_truthy) {
                    sp--;
                    vals[vp - 1] = (val_t) { .type = VAL_BOOL, .v.b = true };
                    val_free(&left);
                } else {
                    f->phase = 2;
                    if (sp >= EVAL_STACK_CAP)
                        goto overflow;
                    stack[sp++] = (eval_frame_t) { cur->v.binary.right, 0 };
                }
            } else {
                sp--;
                val_t right = vals[--vp];
                val_t left = vals[--vp];
                val_t r = { .type = VAL_BOOL };
                switch (cur->v.binary.op) {
                case OP_OR:
                    r.v.b = val_truthy(&left, ctx->src) || val_truthy(&right, ctx->src);
                    break;
                case OP_AND:
                    r.v.b = val_truthy(&left, ctx->src) && val_truthy(&right, ctx->src);
                    break;
                case OP_EQ:
                    r.v.b = val_eq(&left, &right, ctx->src);
                    break;
                case OP_NE:
                    r.v.b = !val_eq(&left, &right, ctx->src);
                    break;
                case OP_LT:
                    r.v.b = val_float(&left, ctx->src) < val_float(&right, ctx->src);
                    break;
                case OP_LE:
                    r.v.b = val_float(&left, ctx->src) <= val_float(&right, ctx->src);
                    break;
                case OP_GT:
                    r.v.b = val_float(&left, ctx->src) > val_float(&right, ctx->src);
                    break;
                case OP_GE:
                    r.v.b = val_float(&left, ctx->src) >= val_float(&right, ctx->src);
                    break;
                case OP_ADD:
                    r.type = VAL_FLOAT;
                    r.v.f = val_float(&left, ctx->src) + val_float(&right, ctx->src);
                    break;
                case OP_SUB:
                    r.type = VAL_FLOAT;
                    r.v.f = val_float(&left, ctx->src) - val_float(&right, ctx->src);
                    break;
                case OP_MUL:
                    r.type = VAL_FLOAT;
                    r.v.f = val_float(&left, ctx->src) * val_float(&right, ctx->src);
                    break;
                case OP_DIV: {
                    double d = val_float(&right, ctx->src);
                    r.type = VAL_FLOAT;
                    r.v.f = (d != 0.0) ? val_float(&left, ctx->src) / d : 0.0;
                    break;
                }
                case OP_NEG:
                case OP_NOT:
                    break;
                }
                val_free(&left);
                val_free(&right);
                vals[vp++] = r;
            }
            break;
        }
    }

    return vp > 0 ? vals[0] : (val_t) { .type = VAL_NULL };

overflow:
    while (vp > 0)
        val_free(&vals[--vp]);
    return (val_t) { .type = VAL_NULL };
}

// #endregion

// #region Public API

CYAML_API cyaml_path_result_t cyaml_path_query(const cyaml_doc_t* doc, const cyaml_node_t* context, const char* path)
{
    cyaml_path_result_t result = { 0 };

    if (!doc || !path) {
        result.error = "null argument";
        return result;
    }
    if (!context)
        context = doc->root;
    if (!context) {
        result.error = "no context node";
        return result;
    }

    expr_pool_t pool = { 0 };
    path_parser_t parser = { .pool = &pool };
    lex_init(&parser.lex, path);
    lex_next(&parser.lex);

    path_t parsed;
    if (!parse_path(&parser, &parsed) || parser.error) {
        result.error = parser.error ? parser.error : "parse error";
        result.error_pos = parser.error_pos;
        return result;
    }

    if (parser.lex.tok.type != TOK_EOF) {
        result.error = "unexpected token";
        result.error_pos = (uint32_t)(parser.lex.tok.start - path);
        return result;
    }

    eval_t ctx = { .doc = doc, .root = doc->root, .current = context, .src = cyaml_src(doc) };
    cyaml_node_t* start = parsed.absolute ? (cyaml_node_t*)doc->root : (cyaml_node_t*)context;
    val_t val = eval_path_on(&ctx, start, parsed.steps, parsed.count);

    if (val.type == VAL_NODES && val.v.nodes.count > 0) {
        result.nodes = malloc(val.v.nodes.count * sizeof(cyaml_node_t*));
        if (result.nodes) {
            memcpy(result.nodes, val.v.nodes.nodes, val.v.nodes.count * sizeof(cyaml_node_t*));
            result.count = val.v.nodes.count;
        }
    }
    val_free(&val);
    return result;
}

CYAML_API void cyaml_path_result_free(cyaml_path_result_t* result)
{
    if (result) {
        free(result->nodes);
        result->nodes = NULL;
        result->count = 0;
    }
}

CYAML_API cyaml_node_t* cyaml_path_first(const cyaml_doc_t* doc, const cyaml_node_t* context, const char* path)
{
    cyaml_path_result_t r = cyaml_path_query(doc, context, path);
    cyaml_node_t* n = r.count > 0 ? r.nodes[0] : NULL;
    cyaml_path_result_free(&r);
    return n;
}

// #endregion

// #region Debug

#ifdef CYAML_DEBUG

static const char* tok_name(tok_t t)
{
    static const char* names[] = {
        "EOF", "SLASH", "DOT", "DOTDOT", "STAR", "STARSTAR", "LBRACKET", "RBRACKET",
        "LPAREN", "RPAREN", "COLON", "QUESTION", "AT", "OR", "AND", "EQ", "NE",
        "LT", "LE", "GT", "GE", "PLUS", "MINUS", "DIV", "BANG", "IDENT", "INT",
        "FLOAT", "STRING", "TRUE", "FALSE", "NULL", "ERROR"
    };
    return t < sizeof(names) / sizeof(names[0]) ? names[t] : "?";
}

static void print_expr(const expr_t* e, int ind);

static void print_steps(const step_t* steps, uint32_t n, int ind)
{
    static const char* snames[] = { "IDENTITY", "PARENT", "WILDCARD", "RECURSIVE", "NAME", "ALIAS", "INDEX", "SLICE", "FILTER" };
    for (uint32_t i = 0; i < n; i++) {
        const step_t* s = &steps[i];
        for (int j = 0; j < ind; j++)
            printf("  ");
        printf("%s", snames[s->type]);
        if (s->type == STEP_NAME || s->type == STEP_ALIAS)
            printf(" '%.*s'", (int)s->v.name.len, s->v.name.s);
        else if (s->type == STEP_INDEX)
            printf(" [%lld]", (long long)s->v.idx);
        else if (s->type == STEP_SLICE)
            printf(" [%lld:%lld:%lld]", (long long)s->v.slice.start, (long long)s->v.slice.end, (long long)s->v.slice.step);
        printf("\n");
        if (s->type == STEP_FILTER)
            print_expr(s->v.filter, ind + 1);
    }
}

static void print_expr(const expr_t* e, int ind)
{
    for (int i = 0; i < ind; i++)
        printf("  ");
    if (!e) {
        printf("(null)\n");
        return;
    }
    static const char* ops[] = { "OR", "AND", "EQ", "NE", "LT", "LE", "GT", "GE", "ADD", "SUB", "MUL", "DIV", "NEG", "NOT" };
    switch (e->type) {
    case EXPR_INT:
        printf("INT %lld\n", (long long)e->v.i);
        break;
    case EXPR_FLOAT:
        printf("FLOAT %f\n", e->v.f);
        break;
    case EXPR_STRING:
        printf("STRING '%.*s'\n", (int)e->v.str.len, e->v.str.s);
        break;
    case EXPR_BOOL:
        printf("BOOL %s\n", e->v.b ? S_TRUE : S_FALSE);
        break;
    case EXPR_NULL:
        printf("NULL\n");
        break;
    case EXPR_PATH:
        printf("PATH (%u steps)\n", e->v.path.count);
        print_steps(e->v.path.steps, e->v.path.count, ind + 1);
        break;
    case EXPR_UNARY:
        printf("UNARY %s\n", ops[e->v.unary.op]);
        print_expr(e->v.unary.arg, ind + 1);
        break;
    case EXPR_BINARY:
        printf("BINARY %s\n", ops[e->v.binary.op]);
        print_expr(e->v.binary.left, ind + 1);
        print_expr(e->v.binary.right, ind + 1);
        break;
    }
}

CYAML_API void cyaml_path_debug(const char* path)
{
    if (!path) {
        printf("(null path)\n");
        return;
    }
    printf("=== Tokens ===\nInput: %s\n", path);

    lexer_t l;
    lex_init(&l, path);
    lex_next(&l);
    while (l.tok.type != TOK_EOF && l.tok.type != TOK_ERROR) {
        printf("  %-12s '%.*s'", tok_name(l.tok.type), (int)l.tok.len, l.tok.start);
        if (l.tok.type == TOK_INT)
            printf(" (val=%lld)", (long long)l.tok.val.i);
        if (l.tok.type == TOK_FLOAT)
            printf(" (val=%f)", l.tok.val.f);
        printf("\n");
        lex_next(&l);
    }
    if (l.tok.type == TOK_ERROR)
        printf("  ERROR: %s\n", l.error);
    printf("  EOF\n\n=== AST ===\n");

    expr_pool_t pool = { 0 };
    path_parser_t p = { .pool = &pool };
    lex_init(&p.lex, path);
    lex_next(&p.lex);
    path_t parsed;
    if (!parse_path(&p, &parsed) || p.error) {
        printf("Parse error: %s at pos %u\n", p.error ? p.error : "unknown", p.error_pos);
        return;
    }
    printf("Path: absolute=%s, steps=%u\n", parsed.absolute ? "yes" : "no", parsed.count);
    print_steps(parsed.steps, parsed.count, 1);
}

#else

CYAML_API void cyaml_path_debug(const char* path) { (void)path; }

#endif

// #endregion
