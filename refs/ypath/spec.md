# YPATH Specification

**Version 1.0**

## Abstract

YPATH is a query language for traversing and selecting nodes within YAML documents. This specification defines the syntax and semantics of YPATH expressions, which evaluate against a context node to produce a node set.

---

## 1. Introduction

### 1.1 Purpose

YAML documents form directed graphs of nodes. YPATH provides a standard notation for selecting nodes within these graphs. Given a document and an expression, a conforming implementation produces the set of all matching nodes.

### 1.2 Scope

This specification defines:

- The data model on which YPATH operates
- The syntax of YPATH expressions
- The semantics of expression evaluation
- Conformance requirements for implementations

### 1.3 Notation

This document uses Extended Backus-Naur Form (EBNF) for grammar definitions:

```
=           definition
|           alternation
[ ... ]     optional (zero or one)
{ ... }     repetition (zero or more)
( ... )     grouping
' ... '     literal character
" ... "     literal string
```

---

## 2. Data Model

### 2.1 Node Types

YPATH operates on the logical structure of a YAML document. Every node has one of three types:

**Scalar**
: An atomic value. Scalars include strings, integers, floats, booleans, and null.

**Sequence**
: An ordered collection of nodes. Elements are indexed by integers starting at zero.

**Mapping**
: A collection of key-value pairs. Keys are unique within a mapping. Key order is preserved but not semantically significant.

### 2.2 Document Root

Every YAML document has exactly one root node. The root may be a scalar, sequence, or mapping. The root serves as the entry point for absolute paths.

### 2.3 Parent Relationships

Every node except the root has exactly one parent. The parent of a sequence element is the sequence. The parent of a mapping value is the mapping. The parent of a mapping key is also the mapping.

### 2.4 Anchors and Aliases

A node may have an anchor, written `&name` in YAML source. An alias, written `*name`, creates a reference to the anchored node. Anchors and aliases enable graph structures within documents.

---

## 3. Expression Structure

### 3.1 Paths

A YPATH expression consists of a sequence of steps separated by `/`. Each step selects nodes based on the current context. The selected nodes become the context for the next step.

There are two path forms:

**Absolute Path**
: Begins with `/`. Evaluation starts from the document root.

**Relative Path**
: Does not begin with `/`. Evaluation starts from the current context node.

### 3.2 Context

Every expression evaluates within a context. The context consists of:

- A context node (the current position in the document)
- A context position (when iterating over a node set)
- A context size (the total nodes in the current set)

### 3.3 Node Sets

Expressions produce node sets. A node set is an ordered collection of zero or more nodes. Nodes appear in document order. A node appears at most once in any node set.

---

## 4. Steps

A step selects nodes relative to the context. Steps fall into several categories.

### 4.1 Identity

The identity step `.` selects the context node itself.

```
.           Selects the current node
```

### 4.2 Parent

The parent step `..` selects the parent of the context node. If the context node is the root, the result is empty.

```
..          Selects the parent node
```

### 4.3 Child Selection

Child selectors retrieve immediate children of the context node.

**Named Child**
: An identifier selects the value from a mapping where the key matches.

```
foo         Selects the value for key "foo"
```

**Wildcard**
: The `*` selector selects all children.

```
*           For a mapping: selects all values
            For a sequence: selects all elements
            For a scalar: selects nothing
```

### 4.4 Recursive Descent

The `**` selector selects the context node and all descendants recursively.

```
**          Selects current node and all descendants
```

When followed by another step, recursive descent finds all nodes at any depth that match the subsequent step.

### 4.5 Alias Dereferencing

The `*` prefix followed by an identifier resolves an alias.

```
*foo        Selects the node anchored as "foo"
```

---

## 5. Bracket Expressions

Bracket expressions provide indexing, slicing, and filtering. They are enclosed in `[` and `]`.

### 5.1 Index Selection

An integer index selects a single element from a sequence.

```
[0]         Selects the first element
[1]         Selects the second element
[-1]        Selects the last element
[-2]        Selects the second-to-last element
```

Positive indices count from the start (zero-based). Negative indices count from the end. Out-of-bounds indices produce an empty result.

### 5.2 Slice Selection

A slice selects a range of elements from a sequence.

```
[start:end]         Elements from start to end (exclusive)
[start:end:step]    Elements from start to end with stride
```

**Parameters:**

| Parameter | Default | Description |
|:----------|:--------|:------------|
| start | 0 | First index (inclusive) |
| end | length | Last index (exclusive) |
| step | 1 | Stride between elements |

**Examples:**

```
[0:3]       First three elements
[1:]        All elements except the first
[:-1]       All elements except the last
[::2]       Every other element
[::-1]      All elements in reverse order
```

### 5.3 Filter Selection

A filter selects nodes that satisfy a predicate. Filters begin with `?`.

```
[?expression]       Selects nodes where expression is true
```

The expression evaluates once for each node in the context. Nodes for which the expression produces a truthy value are included in the result.

---

## 6. Filter Expressions

Filter expressions support comparison, logical, and arithmetic operations.

### 6.1 Current Node Reference

Within a filter, `@` refers to the node being tested.

```
@           The current node under evaluation
@.price     The "price" child of the current node
@[0]        The first element of the current node
```

### 6.2 Operators

Operators are listed from lowest to highest precedence.

**Logical Or**
```
||          True if either operand is true
```

**Logical And**
```
&&          True if both operands are true
```

**Equality**
```
==          True if operands are equal
!=          True if operands are not equal
```

**Relational**
```
<           True if left is less than right
<=          True if left is less than or equal to right
>           True if left is greater than right
>=          True if left is greater than or equal to right
```

**Additive**
```
+           Addition
-           Subtraction
```

**Multiplicative**
```
*           Multiplication
/           Division
```

**Unary**
```
-           Numeric negation
!           Logical negation
```

### 6.3 Literals

Filter expressions may contain literal values.

| Type | Examples |
|:-----|:---------|
| Integer | `0`, `42`, `-17` |
| Float | `3.14`, `-0.5`, `1e10` |
| String | `"hello"`, `'world'` |
| Boolean | `true`, `false` |
| Null | `null` |

### 6.4 Parentheses

Parentheses override operator precedence.

```
(a || b) && c       Evaluates or before and
```

---

## 7. Formal Grammar

### 7.1 Path Productions

```ebnf
path            = absolute_path
                | relative_path ;

absolute_path   = "/" , [ relative_path ] ;

relative_path   = step , { "/" , step } ;
```

### 7.2 Step Productions

```ebnf
step            = identity
                | parent
                | recursive
                | wildcard
                | name
                | alias
                | bracket_expr ;

identity        = "." ;

parent          = ".." ;

recursive       = "**" ;

wildcard        = "*" ;

name            = identifier ;

alias           = "*" , identifier ;

bracket_expr    = "[" , bracket_content , "]" ;

bracket_content = index
                | slice
                | filter ;
```

### 7.3 Bracket Productions

```ebnf
index           = integer ;

slice           = [ integer ] , ":" , [ integer ] , [ ":" , [ integer ] ] ;

filter          = "?" , expression ;
```

### 7.4 Expression Productions

```ebnf
expression      = logical_or ;

logical_or      = logical_and , { "||" , logical_and } ;

logical_and     = equality , { "&&" , equality } ;

equality        = relational , { ( "==" | "!=" ) , relational } ;

relational      = additive , { ( "<" | "<=" | ">" | ">=" ) , additive } ;

additive        = multiplicative , { ( "+" | "-" ) , multiplicative } ;

multiplicative  = unary , { ( "*" | "/" ) , unary } ;

unary           = [ "-" | "!" ] , primary ;

primary         = literal
                | path_expr
                | "(" , expression , ")" ;

path_expr       = "@" , { "/" , step } ;
```

### 7.5 Lexical Productions

```ebnf
literal         = integer
                | float
                | string
                | "true"
                | "false"
                | "null" ;

identifier      = name_start , { name_char } ;

name_start      = letter | "_" ;

name_char       = letter | digit | "_" ;

integer         = [ "-" ] , digit , { digit } ;

float           = integer , "." , digit , { digit } , [ exponent ]
                | integer , exponent ;

exponent        = ( "e" | "E" ) , [ "+" | "-" ] , digit , { digit } ;

string          = double_string | single_string ;

double_string   = '"' , { dq_char | escape } , '"' ;

single_string   = "'" , { sq_char | "''" } , "'" ;

dq_char         = ? any character except " and \ ? ;

sq_char         = ? any character except ' ? ;

escape          = "\" , ( '"' | "\" | "n" | "r" | "t" | "b" | "f" ) ;

letter          = "A" | ... | "Z" | "a" | ... | "z" ;

digit           = "0" | ... | "9" ;
```

---

## 8. Railroad Diagrams

### 8.1 Path

```
              ┌─────┐
         ┌────┤  /  ├────────────────────────┐
         │    └─────┘                        │
         │                                   │
Path ────┼───────────────────────────────────┼────►
         │                                   │
         │    ┌────────┐       ┌─────┐       │
         └────┤  step  ├───┬───┤  /  ├───┐   │
              └────────┘   │   └─────┘   │   │
                           │      │      │   │
                           │      ▼      │   │
                           │  ┌────────┐ │   │
                           │  │  step  ├─┘   │
                           │  └────────┘     │
                           │                 │
                           └─────────────────┘
```

### 8.2 Step

```
              ┌─────────┐
         ┌────┤    .    ├────┐
         │    └─────────┘    │
         │                   │
         │    ┌─────────┐    │
         ├────┤   ..    ├────┤
         │    └─────────┘    │
         │                   │
         │    ┌─────────┐    │
         ├────┤    *    ├────┤
         │    └─────────┘    │
         │                   │
         │    ┌─────────┐    │
Step ────┼────┤   **    ├────┼────►
         │    └─────────┘    │
         │                   │
         │    ┌─────────┐    │
         ├────┤  name   ├────┤
         │    └─────────┘    │
         │                   │
         │  ┌───┐ ┌──────┐   │
         ├──┤ * ├─┤ name ├───┤
         │  └───┘ └──────┘   │
         │                   │
         │  ┌───┐ ┌──────┐ ┌───┐
         └──┤ [ ├─┤ expr ├─┤ ] ├─┘
            └───┘ └──────┘ └───┘
```

### 8.3 Bracket Expression

```
                   ┌─────────────┐
              ┌────┤   integer   ├────┐
              │    └─────────────┘    │
              │                       │
              │    ┌───────────┐      │
Bracket ──────┼────┤   slice   ├──────┼────►
              │    └───────────┘      │
              │                       │
              │  ┌───┐ ┌──────────┐   │
              └──┤ ? ├─┤   expr   ├───┘
                 └───┘ └──────────┘
```

### 8.4 Slice

```
           ┌─────────┐   ┌───┐   ┌─────────┐
Slice ──┬──┤ integer ├─┬─┤ : ├─┬─┤ integer ├─┬──────────────────────┬──►
        │  └─────────┘ │ └───┘ │ └─────────┘ │                      │
        │              │       │             │                      │
        └──────────────┘       └─────────────┤                      │
                                             │  ┌───┐  ┌─────────┐  │
                                             └──┤ : ├──┤ integer ├──┤
                                                └───┘  └─────────┘  │
                                                       │            │
                                                       └────────────┘
```

### 8.5 Filter Expression

```
                 ┌─────────────┐
            ┌────┤   literal   ├────┐
            │    └─────────────┘    │
            │                       │
            │    ┌─────────────┐    │
            ├────┤      @      ├────┤
Primary ────┤    └─────────────┘    ├────►
            │                       │
            │  ┌───┐ ┌──────┐ ┌───┐ │
            └──┤ ( ├─┤ expr ├─┤ ) ├─┘
               └───┘ └──────┘ └───┘
```

### 8.6 Operator Precedence

```
Lowest      ||
            &&
            ==  !=
            <   <=   >   >=
            +   -
            *   /
Highest     -   !   (unary)
```

---

## 9. Evaluation Semantics

### 9.1 Path Evaluation

Path evaluation proceeds step by step:

1. Initialize the context with the starting node.
2. For each step in the path:
   a. Apply the step to each node in the current context.
   b. Collect all selected nodes into a new node set.
   c. Remove duplicates, preserving document order.
   d. The new node set becomes the context for the next step.
3. Return the final node set.

### 9.2 Step Evaluation

Each step type has specific evaluation rules:

**Identity (.)**
: Returns a set containing only the context node.

**Parent (..)**
: Returns a set containing the parent of the context node, or empty if at root.

**Wildcard (*)**
: For mappings, returns all values. For sequences, returns all elements. For scalars, returns empty.

**Recursive (\*\*)**
: Returns the context node plus all descendants in document order.

**Name (identifier)**
: For mappings, returns the value for the matching key. For other types, returns empty.

**Alias (*name)**
: Returns the node with the specified anchor, or empty if not found.

### 9.3 Filter Evaluation

For each node in the context:

1. Bind `@` to the current node.
2. Evaluate the filter expression.
3. If the result is truthy, include the node in the output.

Truthy values: non-empty strings, non-zero numbers, `true`, non-empty node sets.
Falsy values: empty strings, zero, `false`, `null`, empty node sets.

### 9.4 Type Coercion

When operators require specific types:

- Scalars convert to their natural type (string, number, boolean).
- Node sets containing one scalar convert to that scalar's value.
- Other conversions produce `null`.

---

## 10. Examples

### 10.1 Basic Navigation

Document:

```yaml
store:
  name: "Books & Co"
  books:
    - title: "YAML Essentials"
      price: 29.99
    - title: "Data Formats"
      price: 39.99
  location:
    city: "Portland"
    state: "OR"
```

| Expression | Result |
|:-----------|:-------|
| `/` | Document root |
| `/store` | The store mapping |
| `/store/name` | `"Books & Co"` |
| `/store/books` | The books sequence |
| `/store/books[0]` | First book mapping |
| `/store/books[0]/title` | `"YAML Essentials"` |
| `/store/books[-1]/price` | `39.99` |

### 10.2 Wildcards

| Expression | Result |
|:-----------|:-------|
| `/store/*` | All children of store (name, books, location) |
| `/store/books[*]` | All books (same as `/store/books/*`) |
| `/store/books/*/title` | All book titles |

### 10.3 Recursive Descent

| Expression | Result |
|:-----------|:-------|
| `/store/**` | Store and all descendants |
| `/**/title` | All title nodes anywhere in document |
| `/**/price` | All price nodes anywhere in document |

### 10.4 Slices

| Expression | Result |
|:-----------|:-------|
| `/store/books[0:1]` | First book only |
| `/store/books[0:2]` | First two books |
| `/store/books[1:]` | All books except the first |
| `/store/books[:-1]` | All books except the last |
| `/store/books[::-1]` | All books in reverse order |

### 10.5 Filters

| Expression | Result |
|:-----------|:-------|
| `/store/books[?@.price < 35]` | Books priced under 35 |
| `/store/books[?@.price >= 30 && @.price <= 40]` | Books priced 30 to 40 |
| `/store/books[?@.title == "YAML Essentials"]` | Books with matching title |

### 10.6 Anchors and Aliases

Document:

```yaml
defaults: &defaults
  timeout: 30
  retries: 3

production:
  <<: *defaults
  timeout: 60

staging:
  <<: *defaults
```

| Expression | Result |
|:-----------|:-------|
| `/*defaults` | The anchored defaults mapping |
| `/production/timeout` | `60` (overridden value) |
| `/staging/timeout` | `30` (from defaults) |

---

## 11. Conformance

### 11.1 Levels

This specification defines two conformance levels:

**Level 1 (Core)**
: Implementations must support:
- Absolute and relative paths
- Identity, parent, and name steps
- Index selection
- Alias dereferencing

**Level 2 (Full)**
: Implementations must additionally support:
- Wildcard and recursive descent
- Slice selection
- Filter expressions with all operators

### 11.2 Error Handling

Implementations must distinguish:

- **Syntax errors**: The expression is malformed.
- **Type errors**: An operation is applied to an incompatible type.
- **Empty results**: The path is valid but matches no nodes.

Type errors MUST handled by raising errors

---

## Appendix A. Comparison with Related Languages

| Feature | YPATH | JSONPath | XPath 1.0 |
|:--------|:-----:|:--------:|:---------:|
| Root | `/` | `$` | `/` |
| Current node | `.` | `@` | `.` |
| Parent | `..` | N/A | `..` |
| Child | `/name` | `.name` | `/child` |
| Wildcard | `*` | `*` | `*` |
| Recursive | `**` | `..` | `//` |
| Index | `[0]` | `[0]` | `[1]` |
| Slice | `[0:2]` | `[0:2]` | N/A |
| Filter | `[?expr]` | `[?(expr)]` | `[pred]` |
| Filter context | `@` | `@` | `.` |

---

## Appendix B. Reserved for Future Use

The following syntax elements are reserved:

- `$` (variable reference)
- `|` (union operator)
- `~` (type selector)
- Function call syntax `name(...)`

Implementations should reject these as syntax errors until future specification versions define their semantics.
