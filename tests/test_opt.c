#define OPT_IMPLEMENTATION
#define OPT_DEF static inline
#include <opt.h>

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
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

#define OPT_EXPECT_STREQ(expected, actual) \
    do { \
        if (!streq(expected, actual)) { \
            opt_current_test_->success = false; \
            fprintf(stderr, \
                    "%s:%d: OPT_EXPECT_STREQ failed\n" \
                    "    expected: %s = \"%s\"\n" \
                    "    actual:   %s = \"%s\"\n", \
                    __FILE__, __LINE__, \
                    #expected, (expected), \
                    #actual, (actual)); \
        } \
    } while (0)

#define OPT_EXPECT_ENUMEQ(expected, actual, enum_to_str_fn) \
    do { \
        if ((expected) != (actual) || enum_to_str_fn((expected)) == NULL || enum_to_str_fn((actual)) == NULL) { \
            opt_current_test_->success = false; \
            fprintf(stderr, \
                    "%s:%d: OPT_EXPECT_ENUMEQ failed\n" \
                    "    expected: %s = %d (\"%s\")\n" \
                    "    actual:   %s = %d (\"%s\")\n", \
                    __FILE__, __LINE__, \
                    #expected, (expected), enum_to_str_fn((expected)), \
                    #actual, (actual), enum_to_str_fn((actual))); \
        } \
    } while (0)

#define OPT_EXPECT_EQ(expected, actual, type, format) \
    do { \
        type _expected = (expected); \
        type _actual = (actual); \
        if (!(_expected == _actual)) { \
            opt_current_test_->success = false; \
            fprintf(stderr, \
                    "%s:%d: OPT_EXPECT_EQ failed\n" \
                    "    expected: %s = " format "\n" \
                    "    actual:   %s = " format "\n", \
                    __FILE__, __LINE__, \
                    #expected, _expected, \
                    #actual, _actual); \
        } \
    } while (0)

#define OPT_EXPECT_NE(expected, actual, type, format) \
    do { \
        type _expected = (expected); \
        type _actual = (actual); \
        if (_expected == _actual) { \
            opt_current_test_->success = false; \
            fprintf(stderr, \
                    "%s:%d: OPT_EXPECT_NE failed\n" \
                    "    expected: %s = " format "\n" \
                    "    actual:   %s = " format "\n", \
                    __FILE__, __LINE__, \
                    #expected, _expected, \
                    #actual, _actual); \
        } \
    } while (0)

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
        OPT_EXPECT_EQ(token.ignored, false, int, "%d");
        if (token.kind == OPT_TOKEN_POSITIONAL) {
            opts.items[opts.item_count++] = token.raw_arg;
        } else if (token.kind == OPT_TOKEN_END_MARKER) {
        } else if (token.option) {
            if (streq(token.option->name, "create")) opts.command = CMD_CREATE;
            if (streq(token.option->name, "gzip")) opts.compression_method = COMP_GZIP;
        } else {
            OPT_EXPECT_NE(token.option, NULL, void*, "%p");
        }
    }

    OPT_EXPECT_EQ(opts.command, CMD_CREATE, int, "%d");
    OPT_EXPECT_STREQ(opts.change_to_directory, "project");
    OPT_EXPECT_STREQ(opts.file, "archive.tar.gz");
    OPT_EXPECT_EQ(opts.compression_method, COMP_GZIP, int, "%d");
    OPT_EXPECT_EQ(opts.jobs, 2, int, "%d");
    OPT_EXPECT_EQ(opts.verbose, true, bool, "%d");
    OPT_EXPECT_EQ(opts.item_count, 2, int, "%d");
    OPT_EXPECT_STREQ(opts.items[0], "foo");
    OPT_EXPECT_STREQ(opts.items[1], "bar");
}

#define OPT_TESTS \
    X(test_gnu) \

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
