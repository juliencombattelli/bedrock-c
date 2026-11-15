#ifndef BEDROCK_C_BRTEST_H_
#define BEDROCK_C_BRTEST_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    bool success;
} BrTestData_;

// TODO consider a thread local in case of a multithreaded test execution
static BrTestData_ *br_current_test_ = NULL;

#define BR_TEST(test_name)                                 \
    static void br__##test_name##__impl(void);             \
    static int test_name(void) {                           \
        BrTestData_ test_data = {.success = true};         \
        br_current_test_ = &test_data;                     \
        br__##test_name##__impl();                         \
        br_current_test_ = NULL;                           \
        return test_data.success ? 0 : 1;                  \
    }                                                      \
    static void br__##test_name##__impl(void)

typedef enum {
    BR__CMP_EQ,
    BR__CMP_NE,
} BrCmp_;

// Everything known about a check at the call site
typedef struct {
    const char *macro;    // "BR_EXPECT_EQ", ...
    const char *expected; // Argument as written in the source
    const char *actual;
    const char *file;
    int line;
    BrCmp_ cmp;
} BrExpectInfo_;

// Callers stringify the arguments themselves: a macro argument is expanded before
// being forwarded, so stringifying here would print NULL as ((void *)0)
#define BR__INFO(macro, expected_str, actual_str, cmp) \
    ((BrExpectInfo_){macro, expected_str, actual_str, __FILE__, __LINE__, cmp})

// Marks the current test as failed and prints the formatted values, returns false
static bool br__expect_report(const BrExpectInfo_ *info, const char *expected, const char *actual)
{
    br_current_test_->success = false;
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
#define BR__DEFINE_EXPECT_CMP(fn_name, type, format)                               \
    static bool fn_name(BrExpectInfo_ info, type expected, type actual)            \
    {                                                                               \
        bool equal = expected == actual;                                            \
        if (equal == (info.cmp == BR__CMP_EQ)) return true;                        \
        char expected_str[64], actual_str[64];                                      \
        snprintf(expected_str, sizeof expected_str, format, expected);              \
        snprintf(actual_str, sizeof actual_str, format, actual);                    \
        return br__expect_report(&info, expected_str, actual_str);                 \
    }

BR__DEFINE_EXPECT_CMP(br__expect_cmp_int,   long long,             "%lld")
BR__DEFINE_EXPECT_CMP(br__expect_cmp_uint,  unsigned long long,    "%llu")
BR__DEFINE_EXPECT_CMP(br__expect_cmp_float, long double,           "%Lg")
BR__DEFINE_EXPECT_CMP(br__expect_cmp_ptr,   const volatile void *, "%p")

// `1 ? (expected) : (actual)` is never evaluated: it only gives the common type
// `==` would compare in. Every primitive type is listed for completeness, but the
// integer promotions mean _Bool, char and short types actually arrive as int.
// Enums are compatible with an integer type, so they can't have their own entry
// and arrive as int or unsigned. Any pointer falls to default, other types
// (structs, complex numbers, function pointers) fail to compile.
#define BR__EXPECT_CMP_FN(expected, actual) _Generic(1 ? (expected) : (actual), \
    _Bool:              br__expect_cmp_int,   \
    char:               br__expect_cmp_int,   \
    signed char:        br__expect_cmp_int,   \
    short:              br__expect_cmp_int,   \
    int:                br__expect_cmp_int,   \
    long:               br__expect_cmp_int,   \
    long long:          br__expect_cmp_int,   \
    unsigned char:      br__expect_cmp_uint,  \
    unsigned short:     br__expect_cmp_uint,  \
    unsigned:           br__expect_cmp_uint,  \
    unsigned long:      br__expect_cmp_uint,  \
    unsigned long long: br__expect_cmp_uint,  \
    float:              br__expect_cmp_float, \
    double:             br__expect_cmp_float, \
    long double:        br__expect_cmp_float, \
    default:            br__expect_cmp_ptr)

#define BR_EXPECT_EQ(expected, actual) \
    BR__EXPECT_CMP_FN(expected, actual)( \
        BR__INFO("BR_EXPECT_EQ", #expected, #actual, BR__CMP_EQ), (expected), (actual))

#define BR_EXPECT_NE(expected, actual) \
    BR__EXPECT_CMP_FN(expected, actual)( \
        BR__INFO("BR_EXPECT_NE", #expected, #actual, BR__CMP_NE), (expected), (actual))

static bool br__expect_streq(BrExpectInfo_ info, const char *expected, const char *actual)
{
    if (strcmp(expected, actual) == 0) return true;
    br_current_test_->success = false;
    fprintf(stderr,
            "%s:%d: %s failed\n"
            "    expected: %s = \"%s\"\n"
            "    actual:   %s = \"%s\"\n",
            info.file, info.line, info.macro,
            info.expected, expected,
            info.actual, actual);
    return false;
}

#define BR_EXPECT_STREQ(expected, actual) \
    br__expect_streq(BR__INFO("BR_EXPECT_STREQ", #expected, #actual, BR__CMP_EQ), (expected), (actual))

// enum_to_str_fn needs a value of the enum type, which _Generic can't recover
// (enums are compatible with an integer type). Both values are evaluated once
// into these slots, then the comma operator sequences enum_to_str_fn's calls after.
static int br__enum_values_[2];

static bool br__expect_enumeq(BrExpectInfo_ info, int expected, int actual,
                               const char *expected_str, const char *actual_str)
{
    if (expected == actual && expected_str != NULL && actual_str != NULL) return true;
    char expected_buf[128], actual_buf[128];
    snprintf(expected_buf, sizeof expected_buf, "%d (\"%s\")", expected, expected_str ? expected_str : "?");
    snprintf(actual_buf, sizeof actual_buf, "%d (\"%s\")", actual, actual_str ? actual_str : "?");
    return br__expect_report(&info, expected_buf, actual_buf);
}

#define BR_EXPECT_ENUMEQ(expected, actual, enum_to_str_fn) \
    (br__enum_values_[0] = (int)(expected), \
     br__enum_values_[1] = (int)(actual), \
     br__expect_enumeq(BR__INFO("BR_EXPECT_ENUMEQ", #expected, #actual, BR__CMP_EQ), \
                        br__enum_values_[0], br__enum_values_[1], \
                        enum_to_str_fn(br__enum_values_[0]), enum_to_str_fn(br__enum_values_[1])))

#endif // BEDROCK_C_BRTEST_H_
