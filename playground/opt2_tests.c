#define OPT_IMPLEMENTATION
#define OPT_DEF static inline
#include "opt2.h"

#include <stdio.h>
#include <limits.h>

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

static void opt_ensure_impl_(bool condition, const char *condition_str, const char *file, unsigned line)
{
#ifdef OPT_TESTS_DEBUG
    printf("%s:%d: Checking assertion: %s\n", file, line, condition_str);
#endif
    if (!condition) {
        printf("%s:%d: Assertion failed: %s\n", file, line, condition_str);
        opt_current_test_->success = false;
    }
}

#define OPT_ENSURE(condition) \
    opt_ensure_impl_((condition), #condition, __FILE__, __LINE__)

static bool streq(const char *s1, const char *s2)
{
    // NOTE if s1 and s2 are both NULL, they are considered not equal as they are not strings
    if (s1 == NULL || s2 == NULL) return false;
    return strcmp(s1, s2) == 0;
}

////////////////////////////////////////////////////////////////////////////////
/// Opt test suite
////////////////////////////////////////////////////////////////////////////////

static OptToken next_token(OptParser *p, OptTokenKind kind, const char *name)
{
    OptToken token = {0};
    OPT_ENSURE(opt_next(p, &token));
    OPT_ENSURE(token.kind == kind);
    OPT_ENSURE(token.error == OPT_ERROR_NONE);
    OPT_ENSURE(token.option == NULL);
    if (name) {
        OPT_ENSURE(token.name != NULL);
        OPT_ENSURE(token.name_len == strlen(name));
        if (token.name) OPT_ENSURE(strncmp(token.name, name, token.name_len) == 0);
    } else {
        OPT_ENSURE(token.name == NULL);
        OPT_ENSURE(token.name_len == 0);
    }
    return token;
}

static void expect_end(OptParser *p)
{
    OptToken token;
    OPT_ENSURE(!opt_next(p, &token));
    OPT_ENSURE(p->error == OPT_ERROR_NONE);
    OPT_ENSURE(token.error == OPT_ERROR_NONE);
    OPT_ENSURE(p->stream.index == p->stream.argc);
    OPT_ENSURE(p->stream.cluster_body == NULL);
    OPT_ENSURE(p->pending == NULL);
}

typedef struct {
    enum { CMD_NONE, CMD_CREATE, CMD_EXTRACT } command;
    const char *change_to_directory;
    const char *file;
    enum { COMP_NONE, COMP_GZIP, COMP_XZ } compression_method;
    int jobs;
    bool verbose;
    const char *items[2];
    size_t item_count;
} TestGnuOptions;

OPT_TEST(test_gnu)
{
    const char *argv[] = {"tar", "-j2", "-czf", "archive.tar.gz", "-v", "--directory=project", "--", "foo", "bar"};
    TestGnuOptions opts = {0};
    OptParser p = opt_parser_init(opt_scheme_gnu(), (int)OPT_ARRAY_LEN(argv), argv);
    OptToken token = {0};
    while (opt_next(&p, &token)) {
        OPT_ENSURE(token.error == OPT_ERROR_NONE);
        OPT_ENSURE(token.ignored == false);
        if (token.kind == OPT_TOKEN_POSITIONAL) {
            OPT_ENSURE(opts.item_count < OPT_ARRAY_LEN(opts.items));
            if (opts.item_count < OPT_ARRAY_LEN(opts.items)) opts.items[opts.item_count++] = token.raw_arg;
        } else if (token.kind == OPT_TOKEN_END_MARKER) {
            continue;
        } else if (opt_is(&token, 'j', "jobs")) {
            if (!opt_optional_value_int(&p, &opts.jobs) && p.error == OPT_ERROR_NONE)
                opts.jobs = -1;
        } else if (opt_is(&token, 'c', "create")) {
            opts.command = CMD_CREATE;
        } else if (opt_is(&token, 'z', "gzip")) {
            opts.compression_method = COMP_GZIP;
        } else if (opt_is(&token, 'f', "file")) {
            opts.file = opt_value(&p);
        } else if (opt_is(&token, 'v', "verbose")) {
            opts.verbose = true;
        } else if (opt_is(&token, 'C', "directory")) {
            opts.change_to_directory = opt_value(&p);
        } else {
            opt_unexpected(&p, &token);
        }
    }
    OPT_ENSURE(p.error == OPT_ERROR_NONE);
    OPT_ENSURE(opts.command == CMD_CREATE);
    OPT_ENSURE(streq(opts.change_to_directory, "project"));
    OPT_ENSURE(streq(opts.file, "archive.tar.gz"));
    OPT_ENSURE(opts.compression_method == COMP_GZIP);
    OPT_ENSURE(opts.jobs == 2);
    OPT_ENSURE(opts.verbose);
    OPT_ENSURE(opts.item_count == 2);
    OPT_ENSURE(streq(opts.items[0], "foo"));
    OPT_ENSURE(streq(opts.items[1], "bar"));
}

OPT_TEST(test_odin)
{
    const char *build_mode = NULL;
    const char *out = NULL;
    const char *optimization = NULL;
    bool vet = false;
    bool show_timings = false;
    OptScheme scheme = {
        .long_config = {
            .prefixes = {{.prefix = "-"}},
            .assignments = {"=", ":", " "},
        },
        .stop_on.tokens = {"--"},
    };
    const char *argv[] = {"build", "-build-mode:exe", "-out:build/app", "-o=speed", "-vet", "-show-timings", "."};
    OptParser p = opt_parser_init(scheme, (int)OPT_ARRAY_LEN(argv), argv);
    OptToken token;
    size_t positionals = 0;
    while (opt_next(&p, &token)) {
        if (opt_is(&token, 0, "build-mode")) build_mode = opt_value(&p);
        else if (opt_is(&token, 0, "out")) out = opt_value(&p);
        else if (opt_is(&token, 0, "o")) {
            OPT_ENSURE(!opt_is(&token, 'o', NULL));
            optimization = opt_value(&p);
        } else if (opt_is(&token, 0, "vet")) vet = true;
        else if (opt_is(&token, 0, "show-timings")) show_timings = true;
        else if (token.kind == OPT_TOKEN_POSITIONAL) {
            OPT_ENSURE(streq(token.raw_arg, "."));
            ++positionals;
        } else opt_unexpected(&p, &token);
    }
    OPT_ENSURE(p.error == OPT_ERROR_NONE);
    OPT_ENSURE(positionals == 1);
    OPT_ENSURE(streq(build_mode, "exe"));
    OPT_ENSURE(streq(out, "build/app"));
    OPT_ENSURE(streq(optimization, "speed"));
    OPT_ENSURE(vet);
    OPT_ENSURE(show_timings);
}

OPT_TEST(test_inline_value_tokens)
{
    const char *argv[] = {"tool", "--output=a=b", "-v=on", "--empty=", "input"};
    OptParser p = opt_parser_init(opt_scheme_gnu(), (int)OPT_ARRAY_LEN(argv), argv);
    const struct {
        OptTokenKind kind;
        const char *name;
        const char *value;
        int arg_index;
    } expected[] = {
        {OPT_TOKEN_LONG, "output", "a=b", 1},
        {OPT_TOKEN_VALUE, NULL, "a=b", 1},
        {OPT_TOKEN_SHORT, "v", "on", 2},
        {OPT_TOKEN_VALUE, NULL, "on", 2},
        {OPT_TOKEN_LONG, "empty", "", 3},
        {OPT_TOKEN_VALUE, NULL, "", 3},
        {OPT_TOKEN_POSITIONAL, NULL, NULL, 4},
    };
    for (size_t i = 0; i < OPT_ARRAY_LEN(expected); ++i) {
        OptToken t = next_token(&p, expected[i].kind, expected[i].name);
        OPT_ENSURE(t.raw_arg == argv[expected[i].arg_index]);
        OPT_ENSURE(expected[i].value ? streq(t.value, expected[i].value) : t.value == NULL);
        OPT_ENSURE(!t.ignored);
        if (t.name) OPT_ENSURE(t.name > t.raw_arg && t.name < t.raw_arg + strlen(t.raw_arg));
        OPT_ENSURE(!opt_is(&t, 0, NULL));
        OPT_ENSURE(!opt_is(&t, 'O', "OUTPUT"));
        OPT_ENSURE(!opt_is(&t, 0, "out"));
    }
    expect_end(&p);
    expect_end(&p);
}

OPT_TEST(test_required_values)
{
    const char *argv[] = {"tool", "-o", "--", "-j", "-7", "--output=", "tail"};
    OptParser p = opt_parser_init(opt_scheme_gnu(), (int)OPT_ARRAY_LEN(argv), argv);
    next_token(&p, OPT_TOKEN_SHORT, "o");
    OPT_ENSURE(streq(opt_value(&p), "--"));
    OPT_ENSURE(!p.stream.stop_parsing_options);
    next_token(&p, OPT_TOKEN_SHORT, "j");
    int number = 42;
    OPT_ENSURE(opt_value_int(&p, &number));
    OPT_ENSURE(number == -7);
    OptToken t = next_token(&p, OPT_TOKEN_LONG, "output");
    OPT_ENSURE(streq(t.value, ""));
    OPT_ENSURE(streq(opt_value(&p), ""));
    OPT_ENSURE(p.pending == NULL);
    t = next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
    OPT_ENSURE(streq(t.raw_arg, "tail"));
    expect_end(&p);
}

OPT_TEST(test_optional_values)
{
    const char *argv[] = {"tool", "--color", "blue", "-ab", "-c=red", "-oresult", "tail"};
    OptParser p = opt_parser_init(opt_scheme_gnu(), (int)OPT_ARRAY_LEN(argv), argv);
    next_token(&p, OPT_TOKEN_LONG, "color");
    OPT_ENSURE(opt_optional_value(&p) == NULL);
    OPT_ENSURE(p.stream.index == 2);
    next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
    next_token(&p, OPT_TOKEN_SHORT, "a");
    next_token(&p, OPT_TOKEN_SHORT, "b");
    OPT_ENSURE(opt_optional_value(&p) == NULL);
    next_token(&p, OPT_TOKEN_SHORT, "c");
    OPT_ENSURE(streq(opt_optional_value(&p), "red"));
    OPT_ENSURE(opt_optional_value(&p) == NULL);
    next_token(&p, OPT_TOKEN_SHORT, "o");
    OPT_ENSURE(streq(opt_optional_value(&p), "result"));
    OPT_ENSURE(p.stream.sub_index == 0);
    next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
    expect_end(&p);
}

OPT_TEST(test_clustering_and_collating)
{
    for (int collating = 0; collating <= 1; ++collating) {
        for (int clustering = 0; clustering <= 1; ++clustering) {
            OptScheme scheme = opt_scheme_gnu();
            scheme.short_config.allow_collating = collating;
            scheme.short_config.allow_clustering = clustering;
            const char *argv[] = {"tool", "-ab", "value", "-x=explicit"};
            OptParser p = opt_parser_init(scheme, (int)OPT_ARRAY_LEN(argv), argv);
            next_token(&p, OPT_TOKEN_SHORT, "a");
            OPT_ENSURE(p.stream.sub_index == 1);
            OPT_ENSURE(streq(opt_value(&p), collating ? "b" : "value"));
            if (!collating && !clustering) {
                OptToken t;
                OPT_ENSURE(!opt_next(&p, &t));
                OPT_ENSURE(p.error == OPT_ERROR_INVALID_CLUSTER);
                OPT_ENSURE(p.error_arg == argv[1]);
                OPT_ENSURE(t.error == p.error);
                continue;
            }
            if (collating) next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
            else next_token(&p, OPT_TOKEN_SHORT, "b");
            next_token(&p, OPT_TOKEN_SHORT, "x");
            OPT_ENSURE(streq(opt_value(&p), "explicit"));
            expect_end(&p);
        }
    }
    OptScheme scheme = opt_scheme_gnu();
    scheme.short_config.allow_clustering = false;
    const char *argv[] = {"tool", "-ab"};
    OptParser p = opt_parser_init(scheme, (int)OPT_ARRAY_LEN(argv), argv);
    next_token(&p, OPT_TOKEN_SHORT, "a");
    OptToken t;
    OPT_ENSURE(!opt_next(&p, &t));
    OPT_ENSURE(p.error == OPT_ERROR_INVALID_CLUSTER);
}

OPT_TEST(test_assignments)
{
    const struct {
        bool gnu;
        const char *arg;
        OptTokenKind kind;
        const char *name;
        const char *value;
    } cases[] = {
        {false, "--file:a=b", OPT_TOKEN_LONG, "file", "a=b"},
        {true, "--file:a=b", OPT_TOKEN_LONG, "file:a", "b"},
        {false, "--file result", OPT_TOKEN_LONG, "file", "result"},
        {false, "-o:result", OPT_TOKEN_SHORT, "o", "result"},
        {false, "-o result", OPT_TOKEN_SHORT, "o", "result"},
        {true, "-oresult", OPT_TOKEN_SHORT, "o", "result"},
        {true, "-o=", OPT_TOKEN_SHORT, "o", ""},
    };
    for (size_t i = 0; i < OPT_ARRAY_LEN(cases); ++i) {
        const char *argv[] = {"tool", cases[i].arg};
        OptParser p = opt_parser_init(cases[i].gnu ? opt_scheme_gnu() : opt_scheme_builtin(), 2, argv);
        next_token(&p, cases[i].kind, cases[i].name);
        OPT_ENSURE(streq(opt_optional_value(&p), cases[i].value));
        expect_end(&p);
    }
    OptScheme scheme = opt_scheme_gnu();
    scheme.short_config.assignments[1] = "==";
    scheme.long_config.assignments[1] = "==";
    const char *argv[] = {"tool", "--out==a=b", "-o==result", "--name=x==y"};
    OptParser p = opt_parser_init(scheme, (int)OPT_ARRAY_LEN(argv), argv);
    next_token(&p, OPT_TOKEN_LONG, "out");
    OPT_ENSURE(streq(opt_value(&p), "a=b"));
    next_token(&p, OPT_TOKEN_SHORT, "o");
    OPT_ENSURE(streq(opt_value(&p), "result"));
    next_token(&p, OPT_TOKEN_LONG, "name");
    OPT_ENSURE(streq(opt_value(&p), "x==y"));
    expect_end(&p);
}

OPT_TEST(test_prefixes)
{
    OptScheme scheme = {
        .short_config = {
            .prefixes = {{.prefix = "+"}, {.prefix = "-"}},
            .allow_clustering = true,
        },
        .long_config = {
            .prefixes = {{.prefix = "++"}, {.prefix = "+++"}, {.prefix = "--"}},
            .assignments = {"="},
        },
    };
    const char *argv[] = {"tool", "+++long=value", "+ab", "--other", "++", "-", "", "plain"};
    OptParser p = opt_parser_init(scheme, (int)OPT_ARRAY_LEN(argv), argv);
    next_token(&p, OPT_TOKEN_LONG, "long");
    OPT_ENSURE(streq(opt_value(&p), "value"));
    next_token(&p, OPT_TOKEN_SHORT, "a");
    next_token(&p, OPT_TOKEN_SHORT, "b");
    next_token(&p, OPT_TOKEN_LONG, "other");
    for (int i = 4; i < (int)OPT_ARRAY_LEN(argv); ++i) {
        OptToken t = next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
        OPT_ENSURE(t.raw_arg == argv[i]);
    }
    expect_end(&p);
}

OPT_TEST(test_ignored_options)
{
    const char *argv[] = {"tool", "-/vj2", "--/file", "--", "--/color=red", "tail"};
    OptParser p = opt_parser_init(opt_scheme_builtin(), (int)OPT_ARRAY_LEN(argv), argv);
    bool verbose = false;
    int jobs = 10;
    const char *file = "original";
    OptToken t = next_token(&p, OPT_TOKEN_SHORT, "v");
    OPT_ENSURE(t.ignored);
    if (!t.ignored) verbose = true;
    t = next_token(&p, OPT_TOKEN_SHORT, "j");
    OPT_ENSURE(t.ignored);
    int number = 0;
    OPT_ENSURE(opt_value_int(&p, &number));
    OPT_ENSURE(number == 2);
    if (!t.ignored) jobs = number;
    t = next_token(&p, OPT_TOKEN_LONG, "file");
    OPT_ENSURE(t.ignored);
    const char *value = opt_value(&p);
    OPT_ENSURE(streq(value, "--"));
    if (!t.ignored) file = value;
    t = next_token(&p, OPT_TOKEN_LONG, "color");
    OPT_ENSURE(t.ignored);
    t = next_token(&p, OPT_TOKEN_VALUE, NULL);
    OPT_ENSURE(t.ignored);
    OPT_ENSURE(streq(t.value, "red"));
    t = next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
    OPT_ENSURE(!t.ignored);
    OPT_ENSURE(streq(t.raw_arg, "tail"));
    OPT_ENSURE(!verbose);
    OPT_ENSURE(jobs == 10);
    OPT_ENSURE(streq(file, "original"));
    expect_end(&p);

    p = opt_parser_init(opt_scheme_gnu(), 2, argv + 3);
    t = next_token(&p, OPT_TOKEN_LONG, "/color");
    OPT_ENSURE(!t.ignored);
    OPT_ENSURE(streq(opt_value(&p), "red"));
    expect_end(&p);
}

OPT_TEST(test_stop_conditions)
{
    const char *argv[] = {"tool", "-a", "input", "-b", "--", "-c", "--"};
    for (int first = 0; first <= 1; ++first) {
        OptScheme scheme = opt_scheme_gnu();
        scheme.stop_on.first_positional = first;
        OptParser p = opt_parser_init(scheme, (int)OPT_ARRAY_LEN(argv), argv);
        next_token(&p, OPT_TOKEN_SHORT, "a");
        next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
        next_token(&p, first ? OPT_TOKEN_POSITIONAL : OPT_TOKEN_SHORT, first ? NULL : "b");
        next_token(&p, first ? OPT_TOKEN_POSITIONAL : OPT_TOKEN_END_MARKER, NULL);
        next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
        next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
        expect_end(&p);
    }
    OptScheme scheme = opt_scheme_gnu();
    scheme.stop_on.tokens[0] = "-a";
    scheme.stop_on.tokens[1] = "END";
    OptParser p = opt_parser_init(scheme, (int)OPT_ARRAY_LEN(argv), argv);
    next_token(&p, OPT_TOKEN_END_MARKER, NULL);
    for (size_t i = 2; i < OPT_ARRAY_LEN(argv); ++i) next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
    expect_end(&p);
    const char *custom[] = {"tool", "END", "-a"};
    p = opt_parser_init(scheme, (int)OPT_ARRAY_LEN(custom), custom);
    next_token(&p, OPT_TOKEN_END_MARKER, NULL);
    next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
    expect_end(&p);
}

OPT_TEST(test_missing_values)
{
    const char *args[] = {"-o", "--output", "--/output"};
    for (size_t i = 0; i < OPT_ARRAY_LEN(args); ++i) {
        const char *argv[] = {"tool", args[i]};
        OptParser p = opt_parser_init(opt_scheme_builtin(), 2, argv);
        OptToken t;
        OPT_ENSURE(opt_next(&p, &t));
        OPT_ENSURE(opt_value(&p) == NULL);
        OPT_ENSURE(p.error == OPT_ERROR_MISSING_VALUE);
        OPT_ENSURE(p.error_arg == argv[1]);
        OPT_ENSURE(!opt_next(&p, &t));
        OPT_ENSURE(t.error == OPT_ERROR_MISSING_VALUE);
        OPT_ENSURE(t.raw_arg == argv[1]);
        OPT_ENSURE(opt_optional_value(&p) == NULL);
        int number = 73;
        OPT_ENSURE(!opt_value_int(&p, &number));
        OPT_ENSURE(number == 73);
        opt_unexpected(&p, &t);
        opt_fail(&p, &t, "must not overwrite the first error");
        OPT_ENSURE(p.error == OPT_ERROR_MISSING_VALUE);
    }
    const char *argv[] = {"tool", "-j"};
    OptParser p = opt_parser_init(opt_scheme_gnu(), 2, argv);
    next_token(&p, OPT_TOKEN_SHORT, "j");
    int number = 73;
    OPT_ENSURE(!opt_value_int(&p, &number));
    OPT_ENSURE(number == 73);
    OPT_ENSURE(p.error == OPT_ERROR_MISSING_VALUE);
}

OPT_TEST(test_integer_values)
{
    char minimum[64], maximum[64], long_minimum[64], long_maximum[64];
    snprintf(minimum, sizeof(minimum), "%d", INT_MIN);
    snprintf(maximum, sizeof(maximum), "%d", INT_MAX);
    snprintf(long_minimum, sizeof(long_minimum), "%ld", LONG_MIN);
    snprintf(long_maximum, sizeof(long_maximum), "%ld", LONG_MAX);
    const struct {
        const char *value;
        bool valid;
        int number;
    } cases[] = {
        {"0", true, 0}, {"-0", true, 0}, {"+42", true, 42}, {" 12", true, 12},
        {minimum, true, INT_MIN}, {maximum, true, INT_MAX},
        {"", false, 0}, {"+", false, 0}, {"-", false, 0},
        {"abc", false, 0}, {"1x", false, 0}, {"12 ", false, 0}, {"0x10", false, 0},
        {"9999999999999999999999999999999999999999999", false, 0},
        {"-9999999999999999999999999999999999999999999", false, 0},
#if LONG_MAX > INT_MAX
        {long_minimum, false, 0}, {long_maximum, false, 0},
#endif
    };
    for (size_t i = 0; i < OPT_ARRAY_LEN(cases); ++i) {
        const char *argv[] = {"tool", "-j", cases[i].value, "tail"};
        OptParser p = opt_parser_init(opt_scheme_gnu(), (int)OPT_ARRAY_LEN(argv), argv);
        next_token(&p, OPT_TOKEN_SHORT, "j");
        int number = 73;
        errno = ERANGE;
        OPT_ENSURE(opt_value_int(&p, &number) == cases[i].valid);
        OPT_ENSURE(number == (cases[i].valid ? cases[i].number : 73));
        if (cases[i].valid) {
            next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
            expect_end(&p);
        } else {
            OPT_ENSURE(p.error == OPT_ERROR_INVALID_VALUE);
            OPT_ENSURE(p.error_arg == argv[1]);
            OPT_ENSURE(opt_value(&p) == NULL);
            OPT_ENSURE(p.stream.index == 3);
        }

        p = opt_parser_init(opt_scheme_gnu(), (int)OPT_ARRAY_LEN(argv), argv);
        next_token(&p, OPT_TOKEN_SHORT, "j");
        number = 73;
        errno = ERANGE;
        OPT_ENSURE(opt_optional_value_int(&p, &number) == cases[i].valid);
        OPT_ENSURE(number == (cases[i].valid ? cases[i].number : 73));
        OPT_ENSURE(p.error == OPT_ERROR_NONE);
        OPT_ENSURE(p.stream.index == (cases[i].valid ? 3 : 2));
        if (!cases[i].valid) OPT_ENSURE(opt_value(&p) == cases[i].value);
        next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
        expect_end(&p);

        char inline_arg[128];
        snprintf(inline_arg, sizeof(inline_arg), "--jobs=%s", cases[i].value);
        const char *inline_argv[] = {"tool", inline_arg, "2"};
        p = opt_parser_init(opt_scheme_gnu(), (int)OPT_ARRAY_LEN(inline_argv), inline_argv);
        next_token(&p, OPT_TOKEN_LONG, "jobs");
        number = 73;
        errno = ERANGE;
        OPT_ENSURE(opt_optional_value_int(&p, &number) == cases[i].valid);
        OPT_ENSURE(number == (cases[i].valid ? cases[i].number : 73));
        OPT_ENSURE(p.stream.index == 2);
        OPT_ENSURE(p.pending == NULL);
        if (cases[i].valid) {
            next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
            expect_end(&p);
        } else {
            OPT_ENSURE(p.error == OPT_ERROR_INVALID_VALUE);
            OPT_ENSURE(p.error_arg == inline_arg);
            OPT_ENSURE(!opt_optional_value_int(&p, &number));
            OPT_ENSURE(number == 73);
            OPT_ENSURE(p.stream.index == 2);
        }
    }
    const char *argv[] = {"tool", "--jobs=2", "-j3"};
    OptParser p = opt_parser_init(opt_scheme_gnu(), (int)OPT_ARRAY_LEN(argv), argv);
    int number = 0;
    next_token(&p, OPT_TOKEN_LONG, "jobs");
    OPT_ENSURE(opt_value_int(&p, &number));
    OPT_ENSURE(number == 2);
    next_token(&p, OPT_TOKEN_SHORT, "j");
    OPT_ENSURE(opt_value_int(&p, &number));
    OPT_ENSURE(number == 3);
    expect_end(&p);
}

OPT_TEST(test_optional_integer_values)
{
    const char *argv[] = {
        "tool", "-j", "foo", "--jobs", "--verbose", "-j", "--", "2", "-j"
    };
    OptParser p = opt_parser_init(opt_scheme_gnu(), (int)OPT_ARRAY_LEN(argv), argv);
    int number = 73;
    next_token(&p, OPT_TOKEN_SHORT, "j");
    OPT_ENSURE(!opt_optional_value_int(&p, &number));
    OPT_ENSURE(number == 73);
    OPT_ENSURE(p.stream.index == 2);
    next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
    next_token(&p, OPT_TOKEN_LONG, "jobs");
    OPT_ENSURE(!opt_optional_value_int(&p, &number));
    OPT_ENSURE(p.stream.index == 4);
    next_token(&p, OPT_TOKEN_LONG, "verbose");
    next_token(&p, OPT_TOKEN_SHORT, "j");
    OPT_ENSURE(!opt_optional_value_int(&p, &number));
    OPT_ENSURE(p.stream.index == 6);
    next_token(&p, OPT_TOKEN_END_MARKER, NULL);
    next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
    next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
    expect_end(&p);
    OPT_ENSURE(number == 73);

    const char *end_argv[] = {"tool", "-j"};
    p = opt_parser_init(opt_scheme_gnu(), 2, end_argv);
    next_token(&p, OPT_TOKEN_SHORT, "j");
    OPT_ENSURE(!opt_optional_value_int(&p, &number));
    OPT_ENSURE(number == 73);
    expect_end(&p);

    const char *long_argv[] = {"tool", "--jobs", "2", "tail"};
    p = opt_parser_init(opt_scheme_gnu(), (int)OPT_ARRAY_LEN(long_argv), long_argv);
    next_token(&p, OPT_TOKEN_LONG, "jobs");
    OPT_ENSURE(opt_optional_value_int(&p, &number));
    OPT_ENSURE(number == 2);
    OPT_ENSURE(p.stream.index == 3);
    next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
    expect_end(&p);

    const char *attached[] = {"-j2", "-j=2", "--jobs=2", "-vj2"};
    for (size_t i = 0; i < OPT_ARRAY_LEN(attached); ++i) {
        const char *args[] = {"tool", attached[i], "3"};
        p = opt_parser_init(opt_scheme_gnu(), 3, args);
        if (i == 3) next_token(&p, OPT_TOKEN_SHORT, "v");
        next_token(&p, i == 2 ? OPT_TOKEN_LONG : OPT_TOKEN_SHORT, i == 2 ? "jobs" : "j");
        number = 73;
        OPT_ENSURE(opt_optional_value_int(&p, &number));
        OPT_ENSURE(number == 2);
        OPT_ENSURE(p.stream.index == 2);
        OPT_ENSURE(p.stream.cluster_body == NULL);
        next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
        expect_end(&p);
    }

    const char *invalid[] = {"-jfoo", "-j=", "--jobs=foo"};
    for (size_t i = 0; i < OPT_ARRAY_LEN(invalid); ++i) {
        const char *args[] = {"tool", invalid[i], "2"};
        p = opt_parser_init(opt_scheme_gnu(), 3, args);
        next_token(&p, i == 2 ? OPT_TOKEN_LONG : OPT_TOKEN_SHORT, i == 2 ? "jobs" : "j");
        number = 73;
        OPT_ENSURE(!opt_optional_value_int(&p, &number));
        OPT_ENSURE(number == 73);
        OPT_ENSURE(p.error == OPT_ERROR_INVALID_VALUE);
        OPT_ENSURE(p.error_arg == args[1]);
        OPT_ENSURE(p.stream.index == 2);
        OPT_ENSURE(p.stream.cluster_body == NULL);
    }

    OptScheme scheme = opt_scheme_gnu();
    scheme.short_config.allow_collating = false;
    const char *cluster_argv[] = {"tool", "-jv", "2"};
    p = opt_parser_init(scheme, 3, cluster_argv);
    next_token(&p, OPT_TOKEN_SHORT, "j");
    OPT_ENSURE(opt_optional_value_int(&p, &number));
    OPT_ENSURE(number == 2);
    next_token(&p, OPT_TOKEN_SHORT, "v");
    expect_end(&p);

    const char *null_argv[] = {"tool", "--jobs=2", "3"};
    p = opt_parser_init(opt_scheme_gnu(), 3, null_argv);
    next_token(&p, OPT_TOKEN_LONG, "jobs");
    OPT_ENSURE(!opt_optional_value_int(&p, NULL));
    OPT_ENSURE(p.error == OPT_ERROR_INVALID_VALUE);
    OPT_ENSURE(p.error_arg == null_argv[1]);
    OPT_ENSURE(p.pending != NULL);
    OPT_ENSURE(p.stream.index == 2);
    number = 73;
    OPT_ENSURE(!opt_optional_value_int(&p, &number));
    OPT_ENSURE(number == 73);
    OPT_ENSURE(p.pending != NULL);
    OPT_ENSURE(p.stream.index == 2);
}

OPT_TEST(test_error_helpers)
{
    const char *argv[] = {"tool", "--unknown=x", "tail"};
    OptParser p = opt_parser_init(opt_scheme_gnu(), (int)OPT_ARRAY_LEN(argv), argv);
    OptToken t = next_token(&p, OPT_TOKEN_LONG, "unknown");
    opt_unexpected(&p, &t);
    OPT_ENSURE(p.error == OPT_ERROR_UNKNOWN_OPTION);
    OPT_ENSURE(p.error_arg == argv[1]);
    OPT_ENSURE(opt_value(&p) == NULL);
    OPT_ENSURE(!opt_next(&p, &t));
    OPT_ENSURE(t.error == p.error);
    OPT_ENSURE(p.stream.index == 2);

    p = opt_parser_init(opt_scheme_gnu(), (int)OPT_ARRAY_LEN(argv), argv);
    t = next_token(&p, OPT_TOKEN_LONG, "unknown");
    opt_fail(&p, &t, "unsupported option in this command");
    OPT_ENSURE(p.error == OPT_ERROR_INVALID_VALUE);
    OPT_ENSURE(p.error_arg == argv[1]);
    opt_unexpected(&p, &t);
    OPT_ENSURE(p.error == OPT_ERROR_INVALID_VALUE);

    p = opt_parser_init(opt_scheme_gnu(), (int)OPT_ARRAY_LEN(argv), argv);
    next_token(&p, OPT_TOKEN_LONG, "unknown");
    OPT_ENSURE(!opt_value_int(&p, NULL));
    OPT_ENSURE(p.error == OPT_ERROR_INVALID_VALUE);
    OPT_ENSURE(p.pending != NULL);
}

OPT_TEST(test_invalid_option_syntax)
{
    const char *args[] = {"--/","-/","--=value","--/=value"};
    for (size_t i = 0; i < OPT_ARRAY_LEN(args); ++i) {
        const char *argv[] = {"tool", args[i]};
        OptParser p = opt_parser_init(opt_scheme_builtin(), 2, argv);
        OptToken t;
        OPT_ENSURE(!opt_next(&p, &t));
        OPT_ENSURE(p.error == OPT_ERROR_UNKNOWN_OPTION);
        OPT_ENSURE(t.error == p.error);
        OPT_ENSURE(p.error_arg == argv[1]);
    }
}

OPT_TEST(test_scheme_validation)
{
    OptScheme schemes[] = {
        {.short_config.prefixes = {{.prefix = ""}}},
        {.long_config.prefixes = {{.prefix = ""}}},
        {.short_config.assignments = {""}},
        {.long_config.assignments = {""}},
        {.stop_on.tokens = {""}},
        {.short_config.prefixes = {{.prefix = "-"}},
         .long_config.prefixes = {{.prefix = "-"}}},
        {.short_config.prefixes = {{.prefix = "-", .ignore_seq = "/"}, {.prefix = "-"}}},
        {.long_config.prefixes = {{.prefix = "--", .ignore_seq = "/"}, {.prefix = "--", .ignore_seq = "!"}}},
    };
    for (size_t i = 0; i < OPT_ARRAY_LEN(schemes); ++i) {
        OptParser p = opt_parser_init(schemes[i], 0, NULL);
        OPT_ENSURE(p.error == OPT_ERROR_INVALID_VALUE);
        OptToken t;
        OPT_ENSURE(!opt_next(&p, &t));
    }
    OptScheme valid = {
        .short_config.prefixes = {{.prefix = "-"}, {.prefix = "-", .ignore_seq = ""}},
    };
    OptParser p = opt_parser_init(valid, 0, NULL);
    expect_end(&p);
}

OPT_TEST(test_argument_vectors)
{
    OptScheme scheme = opt_scheme_gnu();
    const char *null_program[] = {NULL};
    const char *null_arg[] = {"tool", NULL};
    const struct {
        int argc;
        const char **argv;
    } invalid[] = {{-1, NULL}, {1, NULL}, {1, null_program}, {2, null_arg}};
    for (size_t i = 0; i < OPT_ARRAY_LEN(invalid); ++i) {
        OptParser p = opt_parser_init(scheme, invalid[i].argc, invalid[i].argv);
        OPT_ENSURE(p.error == OPT_ERROR_INVALID_VALUE);
        OptToken t;
        OPT_ENSURE(!opt_next(&p, &t));
    }
    OptParser p = opt_parser_init(scheme, 0, NULL);
    expect_end(&p);
    const char *program[] = {"--not-an-option"};
    p = opt_parser_init(scheme, 1, program);
    expect_end(&p);
    const char *argv[] = {"tool", "-a", "--", ""};
    p = opt_parser_init((OptScheme){0}, (int)OPT_ARRAY_LEN(argv), argv);
    for (size_t i = 1; i < OPT_ARRAY_LEN(argv); ++i) next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
    expect_end(&p);
}

OPT_TEST(test_full_scheme_lists)
{
    OptScheme scheme = {0};
    for (size_t i = 0; i < OPT_SCHEME_LIST_MAX_LEN; ++i) {
        scheme.short_config.prefixes[i].prefix = "-";
        scheme.long_config.prefixes[i].prefix = "--";
        scheme.short_config.assignments[i] = "=";
        scheme.long_config.assignments[i] = "=";
        scheme.stop_on.tokens[i] = "STOP";
    }
    const char *argv[] = {"tool", "-a=x", "--long=y", "STOP", "-b"};
    OptParser p = opt_parser_init(scheme, (int)OPT_ARRAY_LEN(argv), argv);
    next_token(&p, OPT_TOKEN_SHORT, "a");
    OPT_ENSURE(streq(opt_value(&p), "x"));
    next_token(&p, OPT_TOKEN_LONG, "long");
    OPT_ENSURE(streq(opt_value(&p), "y"));
    next_token(&p, OPT_TOKEN_END_MARKER, NULL);
    next_token(&p, OPT_TOKEN_POSITIONAL, NULL);
    expect_end(&p);
}

OPT_TEST(test_independent_parsers)
{
    const char *argv[] = {"tool", "-abresult", "--out=value"};
    OptParser p = opt_parser_init(opt_scheme_gnu(), (int)OPT_ARRAY_LEN(argv), argv);
    OptToken a = next_token(&p, OPT_TOKEN_SHORT, "a");
    OptParser copy = p;
    next_token(&p, OPT_TOKEN_SHORT, "b");
    OPT_ENSURE(streq(opt_value(&p), "result"));
    next_token(&p, OPT_TOKEN_LONG, "out");
    OPT_ENSURE(streq(opt_value(&p), "value"));
    expect_end(&p);
    OPT_ENSURE(opt_is(&a, 'a', NULL));
    next_token(&copy, OPT_TOKEN_SHORT, "b");
    OPT_ENSURE(streq(opt_value(&copy), "result"));
    next_token(&copy, OPT_TOKEN_LONG, "out");
    OptParser pending_copy = copy;
    OPT_ENSURE(streq(opt_value(&copy), "value"));
    OptToken t = next_token(&pending_copy, OPT_TOKEN_VALUE, NULL);
    OPT_ENSURE(streq(t.value, "value"));
    expect_end(&copy);
    expect_end(&pending_copy);
    OPT_ENSURE(streq(argv[1], "-abresult"));
    OPT_ENSURE(streq(argv[2], "--out=value"));
}

OPT_TEST(test_enum_strings)
{
    const char *kinds[] = {"positional", "short", "long", "value", "end-marker"};
    for (size_t i = 0; i < OPT_ARRAY_LEN(kinds); ++i) {
        OPT_ENSURE(streq(opt_token_kind_to_str((OptTokenKind)i), kinds[i]));
    }
    const char *errors[] = {
        "no error", "unknown option", "missing option value", "option does not accept a value",
        "short-option clustering is disabled", "invalid option value",
    };
    for (size_t i = 0; i < OPT_ARRAY_LEN(errors); ++i) {
        OPT_ENSURE(streq(opt_error_to_str((OptError)i), errors[i]));
    }
    OPT_ENSURE(opt_token_kind_to_str((OptTokenKind)-1) == NULL);
    OPT_ENSURE(opt_error_to_str((OptError)-1) == NULL);
}

#define OPT_TESTS \
    X(test_gnu) \
    X(test_odin) \
    X(test_inline_value_tokens) \
    X(test_required_values) \
    X(test_optional_values) \
    X(test_clustering_and_collating) \
    X(test_assignments) \
    X(test_prefixes) \
    X(test_ignored_options) \
    X(test_stop_conditions) \
    X(test_missing_values) \
    X(test_integer_values) \
    X(test_optional_integer_values) \
    X(test_error_helpers) \
    X(test_invalid_option_syntax) \
    X(test_scheme_validation) \
    X(test_argument_vectors) \
    X(test_full_scheme_lists) \
    X(test_independent_parsers) \
    X(test_enum_strings) \

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

int opt2_run_all_tests(void)
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

    return test_failed != 0;
}
