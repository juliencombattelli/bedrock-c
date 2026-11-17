#ifndef BEDROCK_C_BRTEST_H_
#define BEDROCK_C_BRTEST_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>

////////////////////////////////////////////////////////////////////////////////
///                             PUBLIC INTERFACE                             ///
////////////////////////////////////////////////////////////////////////////////

static bool br_test_verbose = false;

#define BR_TEST(test_name) \
    static void br_##test_name##_impl_(void); \
    static int test_name(void) { \
        BrTestData_ test_data = {0}; \
        br_current_test_ = &test_data; \
        br_##test_name##_impl_(); \
        br_current_test_ = NULL; \
        return test_data.expect_failed > 0; \
    } \
    static void br_##test_name##_impl_(void)

#define BR_EXPECT_EQ(actual, expected) \
    BR_EXPECT_CMP_FN_(actual, expected)( \
        BR_INFO_(#expected, #actual, BR_CMP_EQ_), (expected), (actual))

#define BR_EXPECT_NE(actual, expected) \
    BR_EXPECT_CMP_FN_(actual, expected)( \
        BR_INFO_(#expected, #actual, BR_CMP_NE_), (expected), (actual))

#define BR_EXPECT_LT(actual, expected) \
    BR_EXPECT_CMP_FN_(actual, expected)( \
        BR_INFO_(#expected, #actual, BR_CMP_LT_), (expected), (actual))

#define BR_EXPECT_LE(actual, expected) \
    BR_EXPECT_CMP_FN_(actual, expected)( \
        BR_INFO_(#expected, #actual, BR_CMP_LE_), (expected), (actual))

#define BR_EXPECT_GE(actual, expected) \
    BR_EXPECT_CMP_FN_(actual, expected)( \
        BR_INFO_(#expected, #actual, BR_CMP_GE_), (expected), (actual))

#define BR_EXPECT_GT(actual, expected) \
    BR_EXPECT_CMP_FN_(actual, expected)( \
        BR_INFO_(#expected, #actual, BR_CMP_GT_), (expected), (actual))

#define BR_EXPECT_STREQ(actual, expected) \
    br_expect_cmp_str_(BR_INFO_(#expected, #actual, BR_CMP_EQ_), (expected), (actual))

#define BR_EXPECT_STRNE(actual, expected) \
    br_expect_cmp_str_(BR_INFO_(#expected, #actual, BR_CMP_NE_), (expected), (actual))

#define BR_EXPECT_ENUMEQ(actual, expected, enum_to_str_fn) \
    (br_enum_values_[0] = (int)(expected), \
     br_enum_values_[1] = (int)(actual), \
     br_expect_enumeq_(BR_INFO_( #expected, #actual, BR_CMP_EQ_), \
                        br_enum_values_[0], br_enum_values_[1], \
                        enum_to_str_fn(br_enum_values_[0]), enum_to_str_fn(br_enum_values_[1])))


////////////////////////////////////////////////////////////////////////////////
///                          PRIVATE IMPLEMENTATION                          ///
////////////////////////////////////////////////////////////////////////////////

// Common data used by all BR_EXPECT_* functions across a single BR_TEST()
typedef struct {
    size_t expect_success;
    size_t expect_failed;
} BrTestData_;

// TODO consider a thread local in case of a multithreaded test execution
static BrTestData_ *br_current_test_ = NULL;

typedef enum {
    BR_CMP_EQ_,
    BR_CMP_NE_,
    BR_CMP_LT_,
    BR_CMP_LE_,
    BR_CMP_GE_,
    BR_CMP_GT_,
} BrCmp_;

static const char* br_cmd_to_str_(BrCmp_ cmp)
{
    switch (cmp) {
        case BR_CMP_EQ_: return "==";
        case BR_CMP_NE_: return "!=";
        case BR_CMP_LT_: return "<";
        case BR_CMP_LE_: return "<=";
        case BR_CMP_GE_: return ">=";
        case BR_CMP_GT_: return ">";
    }
    return NULL;
}

// Everything known about a check at the call site
typedef struct {
    const char *expected; // Argument as written in the source
    const char *actual;
    const char *file;
    int line;
    BrCmp_ cmp;
} BrExpectInfo_;

// Callers stringify the arguments themselves: a macro argument is expanded before
// being forwarded, so stringifying here would print NULL as ((void *)0)
#define BR_INFO_(expected_str, actual_str, cmp) \
    ((BrExpectInfo_){expected_str, actual_str, __FILE__, __LINE__, cmp})

// Marks the current test as failed and prints the formatted values, returns false
static bool br_expect_report_(bool holds, const BrExpectInfo_ *info, const char *expected, const char *actual)
{
    const char *cmp = br_cmd_to_str_(info->cmp);
    const size_t len_top_left = strlen(info->actual);
    const size_t len_bot_left = strlen(actual);
    const size_t len_max = len_top_left > len_bot_left ? len_top_left : len_bot_left;
    const int padding_left = (int)(len_max > INT_MAX ? INT_MAX : len_max);

    if (holds) {
        br_current_test_->expect_success += 1;
        if (br_test_verbose) {
            fprintf(stderr, "%s:%d: verbose: expectation %s %s %s holds\n",
                info->file, info->line, info->actual, cmp, info->expected);
        }
    } else {
        br_current_test_->expect_failed += 1;
        fprintf(stderr, "%s:%d: error: expected %s %s %s\n",
            info->file, info->line, info->actual, cmp, info->expected);
        fprintf(stderr, "    expected %-*s %s %s\n",
            padding_left, info->actual, cmp, info->expected);
        fprintf(stderr, "      actual %-*s %s %s\n",
            padding_left, actual, cmp, expected);
    }

    return holds;
}

// One comparison function per value category, values are already evaluated
#define BR_DEFINE_EXPECT_CMP_(fn_name, type, format) \
    static bool fn_name(BrExpectInfo_ info, type expected, type actual) \
    { \
        bool holds = false; \
        switch (info.cmp) { \
            case BR_CMP_EQ_: holds = actual == expected; break; \
            case BR_CMP_NE_: holds = actual != expected; break; \
            case BR_CMP_LT_: holds = actual <  expected; break; \
            case BR_CMP_LE_: holds = actual <= expected; break; \
            case BR_CMP_GE_: holds = actual >= expected; break; \
            case BR_CMP_GT_: holds = actual >  expected; break; \
        } \
        char actual_str[64]; \
        snprintf(actual_str, sizeof(actual_str), format, actual); \
        char expected_str[64]; \
        snprintf(expected_str, sizeof(expected_str), format, expected); \
        return br_expect_report_(holds, &info, expected_str, actual_str); \
    }

BR_DEFINE_EXPECT_CMP_(br_expect_cmp_int_,   long long,             "%lld")
BR_DEFINE_EXPECT_CMP_(br_expect_cmp_uint_,  unsigned long long,    "%llu")
BR_DEFINE_EXPECT_CMP_(br_expect_cmp_float_, long double,           "%Lg")
BR_DEFINE_EXPECT_CMP_(br_expect_cmp_ptr_,   const volatile void *, "%p")

static bool br_expect_cmp_str_(BrExpectInfo_ info, const char *expected, const char *actual)
{
    bool holds = false;
    switch (info.cmp) {
        case BR_CMP_EQ_: holds = strcmp(actual, expected) == 0; break;
        case BR_CMP_NE_: holds = strcmp(actual, expected) != 0; break;
        default:
            fprintf(stderr, "%s:%d: Comparator '%s' not supported for strings, aborting.",
                __FILE__, __LINE__, br_cmd_to_str_(info.cmp));
            abort();
    };
    return br_expect_report_(holds, &info, expected, actual); \
}

// enum_to_str_fn needs a value of the enum type, which _Generic can't recover
// (enums are compatible with an integer type). Both values are evaluated once
// into these slots, then the comma operator sequences enum_to_str_fn's calls after.
static int br_enum_values_[2];

static bool br_expect_enumeq_(BrExpectInfo_ info, int expected, int actual,
                              const char *expected_str, const char *actual_str)
{
    bool holds = expected == actual && expected_str != NULL && actual_str != NULL;
    char expected_buf[128], actual_buf[128];
    snprintf(expected_buf, sizeof expected_buf, "%d (\"%s\")", expected, expected_str ? expected_str : "?");
    snprintf(actual_buf, sizeof actual_buf, "%d (\"%s\")", actual, actual_str ? actual_str : "?");
    return br_expect_report_(holds, &info, expected_buf, actual_buf);
}

// `1 ? (expected) : (actual)` is never evaluated: it only gives the common type
// `==`, `<`, ... would compare in. Every primitive type is listed for completeness, but the
// integer promotions mean _Bool, char and short types actually arrive as int.
// Enums are compatible with an integer type, so they can't have their own entry
// and arrive as int or unsigned. Any pointer falls to default, other types
// (structs, complex numbers, function pointers) fail to compile.
#define BR_EXPECT_CMP_FN_(actual, expected) _Generic(1 ? (expected) : (actual), \
    _Bool:              br_expect_cmp_int_,   \
    char:               br_expect_cmp_int_,   \
    signed char:        br_expect_cmp_int_,   \
    short:              br_expect_cmp_int_,   \
    int:                br_expect_cmp_int_,   \
    long:               br_expect_cmp_int_,   \
    long long:          br_expect_cmp_int_,   \
    unsigned char:      br_expect_cmp_uint_,  \
    unsigned short:     br_expect_cmp_uint_,  \
    unsigned:           br_expect_cmp_uint_,  \
    unsigned long:      br_expect_cmp_uint_,  \
    unsigned long long: br_expect_cmp_uint_,  \
    float:              br_expect_cmp_float_, \
    double:             br_expect_cmp_float_, \
    long double:        br_expect_cmp_float_, \
    default:            br_expect_cmp_ptr_)

#endif // BEDROCK_C_BRTEST_H_
