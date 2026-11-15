// Proposal: tests/test_opt.c with OPT_EXPECT_* macros evaluating each argument exactly once.
// Build and run from the repository root:
//     gcc -Iinclude -Wall -Wextra -Wpedantic -std=c11 playground/test_opt_single_eval.c -o out/single_eval && out/single_eval

#define OPT_IMPLEMENTATION
#define OPT_DEF static inline
#include <opt.h>

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

////////////////////////////////////////////////////////////////////////////////
/// Test framework and utilities
////////////////////////////////////////////////////////////////////////////////

typedef struct {
    bool success;
} OptTestData_;

static OptTestData_ *opt_current_test_ = NULL;

#define OPT_TEST(test_name)                                 \
    static void opt__##test_name##__impl(void);             \
    static int test_name(void) {                            \
        OptTestData_ test_data = {.success = true};         \
        opt_current_test_ = &test_data;                     \
        opt__##test_name##__impl();                         \
        opt_current_test_ = NULL;                           \
        return test_data.success ? 0 : 1;                   \
    }                                                       \
    static void opt__##test_name##__impl(void)

// OPT_EXPECT_* macros expand to a single function call returning true on success
// and false on failure, so a check can guard the next one:
//     if (OPT_EXPECT_NE(s, NULL)) OPT_EXPECT_STREQ(s, "foo");
// Every argument is evaluated exactly once, even when the check fails.

typedef enum {
    OPT__CMP_EQ,
    OPT__CMP_NE,
} OptCmp_;

// Everything known about a check at the call site
typedef struct {
    const char *macro;    // "OPT_EXPECT_EQ", ...
    const char *expected; // Argument as written in the source
    const char *actual;
    const char *file;
    int line;
    OptCmp_ cmp;
} OptExpectInfo_;

// Callers stringify the arguments themselves: a macro argument is expanded before
// being forwarded, so stringifying here would print NULL as ((void *)0)
#define OPT__INFO(macro, expected_str, actual_str, cmp) \
    ((OptExpectInfo_){macro, expected_str, actual_str, __FILE__, __LINE__, cmp})

// Marks the current test as failed and prints the formatted values, returns false
static bool opt__expect_report(const OptExpectInfo_ *info, const char *expected, const char *actual)
{
    opt_current_test_->success = false;
    fprintf(stderr,
            "%s:%d: %s failed\n"
            "    expected: %s = %s\n"
            "    actual:   %s = %s\n",
            info->file, info->line, info->macro,
            info->expected, expected,
            info->actual, actual);
    return false;
}

// One comparison function per value category, values are already evaluated
#define OPT__DEFINE_EXPECT_CMP(fn_name, type, format)                               \
    static bool fn_name(OptExpectInfo_ info, type expected, type actual)            \
    {                                                                               \
        bool equal = expected == actual;                                            \
        if (equal == (info.cmp == OPT__CMP_EQ)) return true;                        \
        char expected_str[64], actual_str[64];                                      \
        snprintf(expected_str, sizeof expected_str, format, expected);              \
        snprintf(actual_str, sizeof actual_str, format, actual);                    \
        return opt__expect_report(&info, expected_str, actual_str);                 \
    }

OPT__DEFINE_EXPECT_CMP(opt__expect_cmp_int,   long long,             "%lld")
OPT__DEFINE_EXPECT_CMP(opt__expect_cmp_uint,  unsigned long long,    "%llu")
OPT__DEFINE_EXPECT_CMP(opt__expect_cmp_float, long double,           "%Lg")
OPT__DEFINE_EXPECT_CMP(opt__expect_cmp_ptr,   const volatile void *, "%p")

// `1 ? (expected) : (actual)` is never evaluated: it only gives the common type
// `==` would compare in. Every primitive type is listed for completeness, but the
// integer promotions mean _Bool, char and short types actually arrive as int.
// Enums are compatible with an integer type, so they can't have their own entry
// and arrive as int or unsigned. Any pointer falls to default, other types
// (structs, complex numbers, function pointers) fail to compile.
#define OPT__EXPECT_CMP_FN(expected, actual) _Generic(1 ? (expected) : (actual), \
    _Bool:              opt__expect_cmp_int,   \
    char:               opt__expect_cmp_int,   \
    signed char:        opt__expect_cmp_int,   \
    short:              opt__expect_cmp_int,   \
    int:                opt__expect_cmp_int,   \
    long:               opt__expect_cmp_int,   \
    long long:          opt__expect_cmp_int,   \
    unsigned char:      opt__expect_cmp_uint,  \
    unsigned short:     opt__expect_cmp_uint,  \
    unsigned:           opt__expect_cmp_uint,  \
    unsigned long:      opt__expect_cmp_uint,  \
    unsigned long long: opt__expect_cmp_uint,  \
    float:              opt__expect_cmp_float, \
    double:             opt__expect_cmp_float, \
    long double:        opt__expect_cmp_float, \
    default:            opt__expect_cmp_ptr)

#define OPT_EXPECT_EQ(expected, actual) \
    OPT__EXPECT_CMP_FN(expected, actual)( \
        OPT__INFO("OPT_EXPECT_EQ", #expected, #actual, OPT__CMP_EQ), (expected), (actual))

#define OPT_EXPECT_NE(expected, actual) \
    OPT__EXPECT_CMP_FN(expected, actual)( \
        OPT__INFO("OPT_EXPECT_NE", #expected, #actual, OPT__CMP_NE), (expected), (actual))

// Uses plain strcmp: guard with OPT_EXPECT_NE(s, NULL) when a string may be NULL
static bool opt__expect_streq(OptExpectInfo_ info, const char *expected, const char *actual)
{
    if (strcmp(expected, actual) == 0) return true;
    opt_current_test_->success = false;
    fprintf(stderr,
            "%s:%d: %s failed\n"
            "    expected: %s = \"%s\"\n"
            "    actual:   %s = \"%s\"\n",
            info.file, info.line, info.macro,
            info.expected, expected,
            info.actual, actual);
    return false;
}

#define OPT_EXPECT_STREQ(expected, actual) \
    opt__expect_streq(OPT__INFO("OPT_EXPECT_STREQ", #expected, #actual, OPT__CMP_EQ), (expected), (actual))

// enum_to_str_fn needs a value of the enum type, which _Generic can't recover
// (enums are compatible with an integer type). Both values are evaluated once
// into these slots, then the comma operator sequences enum_to_str_fn's calls after.
static int opt__enum_values_[2];

static bool opt__expect_enumeq(OptExpectInfo_ info, int expected, int actual,
                               const char *expected_str, const char *actual_str)
{
    if (expected == actual && expected_str != NULL && actual_str != NULL) return true;
    char expected_buf[128], actual_buf[128];
    snprintf(expected_buf, sizeof expected_buf, "%d (\"%s\")", expected, expected_str ? expected_str : "?");
    snprintf(actual_buf, sizeof actual_buf, "%d (\"%s\")", actual, actual_str ? actual_str : "?");
    return opt__expect_report(&info, expected_buf, actual_buf);
}

#define OPT_EXPECT_ENUMEQ(expected, actual, enum_to_str_fn) \
    (opt__enum_values_[0] = (int)(expected), \
     opt__enum_values_[1] = (int)(actual), \
     opt__expect_enumeq(OPT__INFO("OPT_EXPECT_ENUMEQ", #expected, #actual, OPT__CMP_EQ), \
                        opt__enum_values_[0], opt__enum_values_[1], \
                        enum_to_str_fn(opt__enum_values_[0]), enum_to_str_fn(opt__enum_values_[1])))

static bool streq(const char *s1, const char *s2)
{
    // NOTE if s1 and s2 are both NULL, they are considered not equal as they are not strings
    if (s1 == NULL || s2 == NULL) return false;
    return strcmp(s1, s2) == 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Opt test suite
////////////////////////////////////////////////////////////////////////////////

static void print_args(int argc, char *argv[])
{
    printf("Args: ");
    for (int i = 0; i < argc; i++) {
        printf("'%s', ", argv[i]);
    }
    printf("\n");
}

typedef struct {
    enum { CMD_NONE, CMD_CREATE, CMD_EXTRACT } command;
    const char *change_to_directory;
    const char *file;
    enum { COMP_NONE, COMP_GZIP, COMP_XZ } compression_method;
    int64_t jobs;
    bool verbose;
    const char *items[2];
    size_t item_count;
} TestGnuOptions;

OPT_TEST(test_gnu)
{
    char *argv[] = {"tar", "-j2", "-czf", "archive.tar.gz", "-v", "--directory=project", "--", "foo", "bar"};
    int argc = sizeof(argv)/sizeof(argv[0]);

    print_args(argc, argv);

    TestGnuOptions opts = {0};

    OptOption options[] = {
        {.name = "jobs", .letter = 'j', .arity = OPT_REQUIRED, OPT_REF(&opts.jobs)},
        {.name = "create", .letter = 'c'},
        {.name = "gzip", .letter = 'z'},
        {.name = "file", .letter = 'f', .arity = OPT_REQUIRED, OPT_REF(&opts.file)},
        {.name = "verbose", .letter = 'v', OPT_REF(&opts.verbose)},
        {.name = "directory", .letter = 'C', .arity = OPT_REQUIRED, OPT_REF(&opts.change_to_directory)},
    };

    OptParser parser = opt_init(argc, argv, options, OPT_ARRAY_LEN(options));

    OptToken token = {0};
    while (opt_parse_next(&parser, &token)) {
        OPT_EXPECT_ENUMEQ(token.error, OPT_ERROR_NONE, opt_error_to_str);
        OPT_EXPECT_EQ(token.ignored, false);
        if (token.kind == OPT_TOKEN_POSITIONAL) {
            opts.items[opts.item_count++] = token.raw_arg;
        } else if (token.kind == OPT_TOKEN_END_MARKER) {
        } else if (token.option) {
            if (streq(token.option->name, "create")) opts.command = CMD_CREATE;
            if (streq(token.option->name, "gzip")) opts.compression_method = COMP_GZIP;
        } else {
            OPT_EXPECT_NE(token.option, NULL);
        }
    }

    OPT_EXPECT_EQ(opts.command, CMD_CREATE);
    if (OPT_EXPECT_NE(opts.change_to_directory, NULL))
        OPT_EXPECT_STREQ(opts.change_to_directory, "project");
    if (OPT_EXPECT_NE(opts.file, NULL))
        OPT_EXPECT_STREQ(opts.file, "archive.tar.gz");
    OPT_EXPECT_EQ(opts.compression_method, COMP_GZIP);
    OPT_EXPECT_EQ(opts.jobs, 2);
    OPT_EXPECT_EQ(opts.verbose, true);
    if (OPT_EXPECT_EQ(opts.item_count, 2)) {
        OPT_EXPECT_STREQ(opts.items[0], "foo");
        OPT_EXPECT_STREQ(opts.items[1], "bar");
    }
}

// Each macro must evaluate its arguments exactly once, also on failure
OPT_TEST(test_expect_single_evaluation)
{
    int calls = 0;
    OPT_EXPECT_EQ(++calls, 1);
    OPT_EXPECT_NE(++calls, 0);
    OPT_EXPECT_STREQ(&"xfoo"[++calls - 2], "foo");
    OPT_EXPECT_ENUMEQ((OptError)(++calls - 4), OPT_ERROR_NONE, opt_error_to_str);
    OPT_EXPECT_EQ(calls, 4);
}

// Every primitive type, alone and mixed with a constant, compiles and compares by value
OPT_TEST(test_expect_primitive_types)
{
    _Bool b = 1;               OPT_EXPECT_EQ(b, (_Bool)1);       OPT_EXPECT_EQ(b, true);
    char c = 'a';              OPT_EXPECT_EQ(c, (char)'a');      OPT_EXPECT_EQ(c, 'a');
    signed char sc = -1;       OPT_EXPECT_EQ(sc, (signed char)-1); OPT_EXPECT_EQ(sc, -1);
    unsigned char uc = 255;    OPT_EXPECT_EQ(uc, (unsigned char)255); OPT_EXPECT_EQ(uc, 255);
    short s = -2;              OPT_EXPECT_EQ(s, (short)-2);      OPT_EXPECT_EQ(s, -2);
    unsigned short us = 2;     OPT_EXPECT_EQ(us, (unsigned short)2); OPT_EXPECT_EQ(us, 2);
    int i = -3;                OPT_EXPECT_EQ(i, -3);
    unsigned u = 3;            OPT_EXPECT_EQ(u, 3u);             OPT_EXPECT_EQ(u, 3);
    long l = -4;               OPT_EXPECT_EQ(l, -4L);            OPT_EXPECT_EQ(l, -4);
    unsigned long ul = 4;      OPT_EXPECT_EQ(ul, 4UL);           OPT_EXPECT_EQ(ul, 4);
    long long ll = LLONG_MIN;  OPT_EXPECT_EQ(ll, LLONG_MIN);     OPT_EXPECT_NE(ll, 0);
    unsigned long long ull = ULLONG_MAX; OPT_EXPECT_EQ(ull, ULLONG_MAX); OPT_EXPECT_NE(ull, 0);
    float f = 1.5f;            OPT_EXPECT_EQ(f, 1.5f);           OPT_EXPECT_EQ(f, 1.5);
    double d = 2.5;            OPT_EXPECT_EQ(d, 2.5);            OPT_EXPECT_NE(d, 2);
    long double ld = 3.5L;     OPT_EXPECT_EQ(ld, 3.5L);          OPT_EXPECT_EQ(ld, 3.5);
    const char *p = "x";       OPT_EXPECT_EQ(p, p);              OPT_EXPECT_NE(p, NULL);
}

#define OPT_TESTS \
    X(test_gnu) \
    X(test_expect_single_evaluation) \
    X(test_expect_primitive_types) \

////////////////////////////////////////////////////////////////////////////////
/// Opt test runner
////////////////////////////////////////////////////////////////////////////////

#define COLOR_RESET     "\033[0m"
#define COLOR_BRIGHT    "\033[1m"
#define COLOR_RED       "\033[1;31m"
#define COLOR_GREEN     "\033[1;32m"

typedef int TestFn(void);

static void run_test(const char *test_name, TestFn *test_fn, unsigned *test_count, unsigned *test_failed)
{
    printf("\n" COLOR_BRIGHT "Running test `%s`..." COLOR_RESET "\n", test_name);
    *test_count += 1;
    int result = (*test_fn)();
    printf(COLOR_BRIGHT "Running test `%s`... ", test_name);
    if (result != 0) {
        *test_failed += 1;
        printf(COLOR_RED "FAILED" COLOR_RESET);
    } else {
        printf(COLOR_GREEN "PASSED" COLOR_RESET);
    }
    printf("\n");
}

int main(void)
{
    unsigned test_count = 0;
    unsigned test_failed = 0;

    #define X(name) run_test(#name, &name, &test_count, &test_failed);
        OPT_TESTS
    #undef X

    printf("\n" COLOR_BRIGHT "Summary:" COLOR_RESET "\n");
    printf("  "
        COLOR_BRIGHT "%u" COLOR_RESET " tests run, "
        COLOR_GREEN "%u" COLOR_RESET " passed, "
        COLOR_RED "%u" COLOR_RESET " failed\n",
        test_count, test_count - test_failed, test_failed);

    return test_failed;
}
