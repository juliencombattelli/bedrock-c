#include <brtest.h>
#include <limits.h>
#include <stdlib.h>

#define TEST_ENUM \
    X(TEST_ENUM_0) \
    X(TEST_ENUM_1) \
    X(TEST_ENUM_2) \
    X(TEST_ENUM_3) \
    X(TEST_ENUM_4) \

typedef enum {
    #define X(e) e,
    TEST_ENUM
    #undef X
} TestEnum;

const char* test_enum_to_str(TestEnum e)
{
    switch(e) {
        #define X(e) case e: return #e;
        TEST_ENUM
        #undef X
    }
    abort();
}

// Each macro must evaluate its arguments exactly once, also on failure
BR_TEST(test_expect_single_evaluation)
{
    int calls = 0;
    BR_EXPECT_EQ(++calls, 1);
    BR_EXPECT_NE(++calls, 0);
    BR_EXPECT_STREQ(&"xfoo"[++calls - 2], "foo");
    BR_EXPECT_ENUMEQ((TestEnum)(++calls - 3), TEST_ENUM_1, test_enum_to_str);
    BR_EXPECT_EQ(calls, 4);
}

typedef signed char schar;
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef unsigned long long ullong;
typedef long long llong;
typedef long double ldouble;

// Every primitive type, alone and mixed with a constant, compiles and compares by value
BR_TEST(test_expect_primitive_types)
{
    _Bool b = 1;             BR_EXPECT_EQ(b, (_Bool)1);     BR_EXPECT_EQ(b, true);
    char c = 'a';            BR_EXPECT_EQ(c, (char)'a');    BR_EXPECT_EQ(c, 'a');
    schar sc = -1;           BR_EXPECT_EQ(sc, (schar)-1);   BR_EXPECT_EQ(sc, -1);
    uchar uc = 255;          BR_EXPECT_EQ(uc, (uchar)255);  BR_EXPECT_EQ(uc, 255);
    short s = -2;            BR_EXPECT_EQ(s, (short)-2);    BR_EXPECT_EQ(s, -2);
    ushort us = 2;           BR_EXPECT_EQ(us, (ushort)2);   BR_EXPECT_EQ(us, 2);
    int i = -3;              BR_EXPECT_EQ(i, -3);
    uint u = 3;              BR_EXPECT_EQ(u, 3u);           BR_EXPECT_EQ(u, 3);
    long l = -4;             BR_EXPECT_EQ(l, -4L);          BR_EXPECT_EQ(l, -4);
    ulong ul = 4;            BR_EXPECT_EQ(ul, 4UL);         BR_EXPECT_EQ(ul, 4);
    llong ll = LLONG_MIN;    BR_EXPECT_EQ(ll, LLONG_MIN);   BR_EXPECT_NE(ll, 0);
    ullong ull = ULLONG_MAX; BR_EXPECT_EQ(ull, ULLONG_MAX); BR_EXPECT_NE(ull, 0);
    float f = 1.5f;          BR_EXPECT_EQ(f, 1.5f);         BR_EXPECT_EQ(f, 1.5);
    double d = 2.5;          BR_EXPECT_EQ(d, 2.5);          BR_EXPECT_NE(d, 2);
    ldouble ld = 3.5L;       BR_EXPECT_EQ(ld, 3.5L);        BR_EXPECT_EQ(ld, 3.5);
    const char *p = "x";     BR_EXPECT_EQ(p, p);            BR_EXPECT_NE(p, NULL);
}

#define BR_TESTS \
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
    // br_test_verbose = true;

    unsigned test_count = 0;
    unsigned test_failed = 0;

    #define X(name) run_test(#name, &name, &test_count, &test_failed);
        BR_TESTS
    #undef X

    printf("\n" COLOR_BRIGHT "Summary:" COLOR_RESET "\n");
    printf("  "
        COLOR_BRIGHT "%u" COLOR_RESET " tests run, "
        COLOR_GREEN "%u" COLOR_RESET " passed, "
        COLOR_RED "%u" COLOR_RESET " failed\n",
        test_count, test_count - test_failed, test_failed);

    return test_failed;
}
