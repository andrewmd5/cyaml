#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

// #region Allocation Tracking

static struct {
    size_t malloc_count;
    size_t calloc_count;
    size_t realloc_count;
    size_t free_count;
    size_t bytes_allocated;
    size_t bytes_freed;
    size_t current_bytes;
    size_t peak_bytes;
    bool enabled;
} alloc_stats = { 0 };

static void alloc_stats_reset(void)
{
    alloc_stats.malloc_count = 0;
    alloc_stats.calloc_count = 0;
    alloc_stats.realloc_count = 0;
    alloc_stats.free_count = 0;
    alloc_stats.bytes_allocated = 0;
    alloc_stats.bytes_freed = 0;
    alloc_stats.current_bytes = 0;
    alloc_stats.peak_bytes = 0;
}

static void alloc_stats_enable(void) { alloc_stats.enabled = true; }
static void alloc_stats_disable(void) { alloc_stats.enabled = false; }

typedef struct {
    size_t size;
    char data[];
} tracked_block_t;

static void* tracked_malloc(size_t size)
{
    tracked_block_t* block = malloc(sizeof(tracked_block_t) + size);
    if (!block)
        return NULL;
    block->size = size;
    if (alloc_stats.enabled) {
        alloc_stats.malloc_count++;
        alloc_stats.bytes_allocated += size;
        alloc_stats.current_bytes += size;
        if (alloc_stats.current_bytes > alloc_stats.peak_bytes)
            alloc_stats.peak_bytes = alloc_stats.current_bytes;
    }
    return block->data;
}

static void* tracked_calloc(size_t count, size_t size)
{
    size_t total = count * size;
    void* p = tracked_malloc(total);
    if (p)
        memset(p, 0, total);
    if (alloc_stats.enabled) {
        alloc_stats.malloc_count--;
        alloc_stats.calloc_count++;
    }
    return p;
}

static void* tracked_realloc(void* ptr, size_t size)
{
    if (!ptr)
        return tracked_malloc(size);
    tracked_block_t* old_block = (tracked_block_t*)((char*)ptr - offsetof(tracked_block_t, data));
    size_t old_size = old_block->size;
    tracked_block_t* new_block = realloc(old_block, sizeof(tracked_block_t) + size);
    if (!new_block)
        return NULL;
    new_block->size = size;
    if (alloc_stats.enabled) {
        alloc_stats.realloc_count++;
        alloc_stats.current_bytes -= old_size;
        alloc_stats.current_bytes += size;
        if (size > old_size)
            alloc_stats.bytes_allocated += size - old_size;
        else
            alloc_stats.bytes_freed += old_size - size;
        if (alloc_stats.current_bytes > alloc_stats.peak_bytes)
            alloc_stats.peak_bytes = alloc_stats.current_bytes;
    }
    return new_block->data;
}

static void tracked_free(void* ptr)
{
    if (!ptr)
        return;
    tracked_block_t* block = (tracked_block_t*)((char*)ptr - offsetof(tracked_block_t, data));
    if (alloc_stats.enabled) {
        alloc_stats.free_count++;
        alloc_stats.bytes_freed += block->size;
        alloc_stats.current_bytes -= block->size;
    }
    free(block);
}

// Redirect library allocations
#define malloc tracked_malloc
#define calloc tracked_calloc
#define realloc tracked_realloc
#define free tracked_free

// Include library sources directly
#include "../src/cyaml.c"
#include "../src/cyaml_emitter.c"
#include "../src/cyaml_events.c"
#include "../src/cyaml_json.c"
#include "../src/cyaml_parser.c"
#include "../src/cyaml_utf8.c"

#undef malloc
#undef calloc
#undef realloc
#undef free

// #endregion

// #region Timing

static uint64_t time_now(void)
{
#ifdef __APPLE__
    return mach_absolute_time();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

static double time_to_ns(uint64_t elapsed)
{
#ifdef __APPLE__
    static mach_timebase_info_data_t info = { 0 };
    if (info.denom == 0)
        mach_timebase_info(&info);
    return (double)elapsed * (double)info.numer / (double)info.denom;
#else
    return (double)elapsed;
#endif
}

// #endregion

// #region Memory

static size_t get_memory_usage(void)
{
#ifdef __APPLE__
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
        return info.resident_size;
    }
    return 0;
#else
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return (size_t)usage.ru_maxrss * 1024;
    }
    return 0;
#endif
}

// #endregion

// #region File Loading

typedef struct {
    char* data;
    size_t len;
    char name[64];
} test_file_t;

typedef struct {
    test_file_t* files;
    size_t count;
    size_t cap;
    size_t total_bytes;
} test_suite_t;

static char* read_file(const char* path, size_t* len)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[n] = '\0';
    if (len)
        *len = n;
    return buf;
}

static void suite_add(test_suite_t* s, const char* path, const char* name)
{
    size_t len;
    char* data = read_file(path, &len);
    if (!data)
        return;

    if (s->count >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 256;
        s->files = realloc(s->files, s->cap * sizeof(test_file_t));
    }

    test_file_t* tf = &s->files[s->count++];
    tf->data = data;
    tf->len = len;
    snprintf(tf->name, sizeof(tf->name), "%s", name);
    s->total_bytes += len;
}

static void suite_load(test_suite_t* s, const char* dir)
{
    memset(s, 0, sizeof(*s));

    DIR* d = opendir(dir);
    if (!d)
        return;

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.')
            continue;

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);

        DIR* sd = opendir(path);
        if (!sd)
            continue;

        char subpath[PATH_MAX];
        snprintf(subpath, sizeof(subpath), "%s/00", path);
        DIR* multi = opendir(subpath);

        if (multi) {
            closedir(multi);
            struct dirent* sent;
            while ((sent = readdir(sd)) != NULL) {
                if (sent->d_name[0] < '0' || sent->d_name[0] > '9')
                    continue;
                char yaml_path[PATH_MAX];
                snprintf(yaml_path, sizeof(yaml_path), "%s/%s/in.yaml", path, sent->d_name);
                char name[64];
                snprintf(name, sizeof(name), "%s/%s", ent->d_name, sent->d_name);
                suite_add(s, yaml_path, name);
            }
        } else {
            char yaml_path[PATH_MAX];
            snprintf(yaml_path, sizeof(yaml_path), "%s/in.yaml", path);
            suite_add(s, yaml_path, ent->d_name);
        }
        closedir(sd);
    }
    closedir(d);
}

static void suite_free(test_suite_t* s)
{
    for (size_t i = 0; i < s->count; i++) {
        free(s->files[i].data);
    }
    free(s->files);
    memset(s, 0, sizeof(*s));
}

// #endregion

// #region Benchmark

typedef struct {
    uint64_t time_ns;
    size_t iterations;
    size_t bytes_parsed;
    size_t files_parsed;
    size_t parse_errors;
    size_t mem_before;
    size_t mem_after;
    size_t malloc_count;
    size_t calloc_count;
    size_t realloc_count;
    size_t free_count;
    size_t alloc_bytes;
    size_t peak_bytes;
} bench_result_t;

static void bench_parse(test_suite_t* suite, int iterations, bench_result_t* r)
{
    memset(r, 0, sizeof(*r));

    r->mem_before = get_memory_usage();
    alloc_stats_reset();
    alloc_stats_enable();

    uint64_t start = time_now();

    for (int iter = 0; iter < iterations; iter++) {
        for (size_t i = 0; i < suite->count; i++) {
            test_file_t* tf = &suite->files[i];
            cyaml_stream_t* stream = cyaml_parse_stream(tf->data, tf->len, NULL, NULL);
            if (stream) {
                cyaml_stream_free(stream);
                r->files_parsed++;
            } else {
                r->parse_errors++;
            }
            r->bytes_parsed += tf->len;
        }
    }

    r->time_ns = time_now() - start;
    alloc_stats_disable();

    r->mem_after = get_memory_usage();
    r->iterations = (size_t)iterations;
    r->malloc_count = alloc_stats.malloc_count;
    r->calloc_count = alloc_stats.calloc_count;
    r->realloc_count = alloc_stats.realloc_count;
    r->free_count = alloc_stats.free_count;
    r->alloc_bytes = alloc_stats.bytes_allocated;
    r->peak_bytes = alloc_stats.peak_bytes;
}

static void print_result(bench_result_t* r, const char* label)
{
    double ns = time_to_ns(r->time_ns);
    double ms = ns / 1e6;
    double sec = ns / 1e9;
    double mb = (double)r->bytes_parsed / (1024.0 * 1024.0);
    double throughput = sec > 0 ? mb / sec : 0;
    double files_per_sec = sec > 0 ? (double)r->files_parsed / sec : 0;
    double ns_per_file = r->files_parsed > 0 ? ns / (double)r->files_parsed : 0;
    double mem_delta = (double)(r->mem_after - r->mem_before) / 1024.0;

    double allocs_per_file = r->files_parsed > 0 ? (double)(r->malloc_count + r->calloc_count) / (double)r->files_parsed : 0;
    double bytes_per_file = r->files_parsed > 0 ? (double)r->alloc_bytes / (double)r->files_parsed : 0;
    double alloc_ratio = (double)r->bytes_parsed > 0 ? (double)r->alloc_bytes / (double)r->bytes_parsed : 0;

    printf("\n%s\n", label);
    printf("  Time:        %.2f ms (%.3f s)\n", ms, sec);
    printf("  Iterations:  %zu\n", r->iterations);
    printf("  Files:       %zu parsed, %zu errors\n", r->files_parsed, r->parse_errors);
    printf("  Data:        %.2f MB\n", mb);
    printf("  Throughput:  %.2f MB/s, %.0f files/s\n", throughput, files_per_sec);
    printf("  Latency:     %.0f ns/file, %.2f us/file\n", ns_per_file, ns_per_file / 1000.0);
    printf("  Memory:      %.0f KB resident (delta: %.0f KB)\n",
        (double)r->mem_after / 1024.0, mem_delta);
    printf("\n  Allocations:\n");
    printf("    malloc:    %zu calls\n", r->malloc_count);
    printf("    calloc:    %zu calls\n", r->calloc_count);
    printf("    realloc:   %zu calls\n", r->realloc_count);
    printf("    free:      %zu calls\n", r->free_count);
    printf("    total:     %.2f MB allocated\n", (double)r->alloc_bytes / (1024.0 * 1024.0));
    printf("    peak:      %.2f KB live at once\n", (double)r->peak_bytes / 1024.0);
    printf("\n  Per-file averages:\n");
    printf("    allocs:    %.1f per file\n", allocs_per_file);
    printf("    bytes:     %.0f bytes per file\n", bytes_per_file);
    printf("    ratio:     %.1fx input size\n", alloc_ratio);
}

// #endregion

// #region Main

static void usage(const char* prog)
{
    fprintf(stderr, "Usage: %s [options] [suite_path]\n", prog);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -n <count>   Number of iterations (default: 100)\n");
    fprintf(stderr, "  -w <count>   Warmup iterations (default: 10)\n");
    fprintf(stderr, "  -q           Quiet mode (summary only)\n");
    fprintf(stderr, "  -h           Show this help\n");
}

int main(int argc, char** argv)
{
    const char* suite_dir = "../refs/yaml-test-suite/data";
    int iterations = 100;
    int warmup = 10;
    bool quiet = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            iterations = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
            warmup = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-q") == 0) {
            quiet = true;
        } else if (strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            suite_dir = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    printf("CYAML Benchmark\n");
    printf("===============\n");
    printf("Version:    %s\n", cyaml_version());
    printf("Suite:      %s\n", suite_dir);
    printf("Iterations: %d (warmup: %d)\n", iterations, warmup);

    test_suite_t suite;
    suite_load(&suite, suite_dir);

    if (suite.count == 0) {
        fprintf(stderr, "No test files found in %s\n", suite_dir);
        return 1;
    }

    printf("Files:      %zu (%.2f KB total)\n", suite.count, (double)suite.total_bytes / 1024.0);

    bench_result_t warmup_result, result;

    if (!quiet)
        printf("\nWarmup...\n");
    bench_parse(&suite, warmup, &warmup_result);
    if (!quiet)
        print_result(&warmup_result, "Warmup");

    if (!quiet)
        printf("\nBenchmark...\n");
    bench_parse(&suite, iterations, &result);
    print_result(&result, "Result");

    suite_free(&suite);
    return 0;
}

// #endregion
