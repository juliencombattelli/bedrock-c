#ifndef OPT_H_
#define OPT_H_

////////////////////////////////////////////////////////////////////////////////
///                          PUBLIC IMPLEMENTATION                           ///
////////////////////////////////////////////////////////////////////////////////

#ifndef OPT_DEF
#define OPT_DEF
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OPT_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

typedef enum {
    OPT_FLAG,
    OPT_REQUIRED,
    OPT_OPTIONAL,
    OPT_COUNTER, // optional int value xor repeating occurrence counter
} OptArity;

typedef enum {
    OPT_BOOL, // bool
    OPT_STR,  // const char* and char*
    OPT_INT,  // int64_t
    OPT_UINT, // uint64_t
} OptValueType;

typedef struct {
    const char *name; // Long option name
    char letter; // Short option name
    OptArity arity;
    OptValueType type;
    void *ref;
    const char *implicit; // OPT_OPTIONAL only: value used when option given without a value
    bool seen;
} OptOption;

// Sets OptOption.type and OptOption.ref from a pointer to the variable receiving the value.
// Supported pointer types:
//   bool*                -> OPT_BOOL
//   const char**, char** -> OPT_STR (points into argv or the implicit value: do not modify or free)
//   int64_t*             -> OPT_INT
//   uint64_t*            -> OPT_UINT
// Any other type (int*, long*, ...) are not supported and may fail to compile.
#define OPT_REF(ptr) \
    .type = _Generic((ptr), \
        bool*:        OPT_BOOL, \
        const char**: OPT_STR, \
        char**:       OPT_STR, \
        int64_t*:     OPT_INT, \
        uint64_t*:    OPT_UINT \
    ), \
    .ref = (void*)(ptr)

typedef enum {
    OPT_TOKEN_POSITIONAL,
    OPT_TOKEN_SHORT,
    OPT_TOKEN_LONG,
    OPT_TOKEN_END_MARKER,
} OptTokenKind;

OPT_DEF const char* opt_token_kind_to_str(OptTokenKind token_kind);

typedef enum {
    OPT_ERROR_NONE,
    OPT_ERROR_UNKNOWN_OPTION,
    OPT_ERROR_MISSING_VALUE,
    OPT_ERROR_INVALID_VALUE,
    OPT_ERROR_MISSING_NAME, // "-/", "--/", "--=x", "-=x": option prefix without a name
} OptError;

OPT_DEF const char* opt_error_to_str(OptError error);

typedef struct {
    OptTokenKind kind;
    const char *raw_arg;
    const char *name;  // Parsed name, borrowed from raw_arg (not NUL-terminated, see name_len)
    size_t name_len;
    const char *value; // Inline value or next argument, always null-terminated
    bool ignored;
    OptOption *option;
    OptError error;
} OptToken;

typedef struct {
    OptOption *options;
    size_t option_count;
    size_t argc;
    char **argv;
    size_t index;
    const char *cluster;  // Rest of the current short-option cluster, NULL if none
    bool cluster_ignored; // Current cluster started with "-/"
    bool stop_parsing_options;
} OptParser;

OPT_DEF OptParser opt_init(int argc, char **argv, OptOption *options, size_t option_count);

OPT_DEF bool opt_parse_next(OptParser *parser, OptToken *token);

OPT_DEF bool opt_was_seen_name(OptParser *parser, const char *name);
OPT_DEF bool opt_was_seen_letter(OptParser *parser, char letter);

////////////////////////////////////////////////////////////////////////////////
///                          PRIVATE IMPLEMENTATION                          ///
///              Internals below -- not part of the public API               ///
///                    Content may change without notice                     ///
///                                                                          ///
/// Define the macro OPT_IMPLEMENTATION in one (and only one) of your        ///
/// compiled files before including this header.                             ///
////////////////////////////////////////////////////////////////////////////////
#ifdef OPT_IMPLEMENTATION

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

OPT_DEF const char* opt_token_kind_to_str(OptTokenKind token_kind)
{
    switch (token_kind) {
        case OPT_TOKEN_POSITIONAL: return "positional";
        case OPT_TOKEN_SHORT:      return "short";
        case OPT_TOKEN_LONG:       return "long";
        case OPT_TOKEN_END_MARKER: return "end-marker";
    }
    return NULL;
}

OPT_DEF const char* opt_error_to_str(OptError error)
{
    switch (error) {
        case OPT_ERROR_NONE:           return "no error";
        case OPT_ERROR_UNKNOWN_OPTION: return "unknown option";
        case OPT_ERROR_MISSING_VALUE:  return "missing option value";
        case OPT_ERROR_INVALID_VALUE:  return "invalid value";
        case OPT_ERROR_MISSING_NAME:   return "missing option name";
    }
    return NULL;
}

#define OPT__ASSIGNMENT_CHARS "=:"

// strtoll/strtoull skip leading whitespace: reject it so " 7" fails like "7 "
static bool opt__starts_like_number(const char *value)
{
    char c = value[0];
    return (c >= '0' && c <= '9') || c == '+' || c == '-';
}

// strtoll/strtoull report overflow through errno: save and restore it so the caller's errno is untouched
static bool opt__parse_int(const char *value, int64_t *out)
{
    if (!value || !opt__starts_like_number(value)) return false;
    int saved_errno = errno;
    char *end = NULL;
    errno = 0;
    long long number = strtoll(value, &end, 10);
    bool valid = end != value && *end == '\0' && errno != ERANGE;
    errno = saved_errno;
    if (valid) *out = (int64_t)number;
    return valid;
}

static bool opt__parse_uint(const char *value, uint64_t *out)
{
    if (!value || !opt__starts_like_number(value)) return false;
    int saved_errno = errno;
    bool negative = strtoll(value, NULL, 10) < 0; // strtoull would silently wrap "-1"
    char *end = NULL;
    errno = 0;
    unsigned long long number = strtoull(value, &end, 10);
    bool valid = !negative && end != value && *end == '\0' && errno != ERANGE;
    errno = saved_errno;
    if (valid) *out = (uint64_t)number;
    return valid;
}

static bool opt__parse_bool(const char *value, bool *out)
{
    // Try common boolean words
    static const char *yes[] = {"yes", "true",  "on",  "y"};
    static const char *no[] =  {"no",  "false", "off", "n"};
    _Static_assert(sizeof(yes) == sizeof(no), "Alternative token count for yes and no must match");
    for (size_t i = 0; i < OPT_ARRAY_LEN(yes); i++) {
        if (strcasecmp(value, yes[i]) == 0) {
            *out = true;
            return true;
        }
        if (strcasecmp(value, no[i]) == 0) {
            *out = false;
            return true;
        }
    }
    // Try C boolean integer logic
    int64_t i = 0;
    if (opt__parse_int(value, &i)) {
        if (i == 0) {
            *out = false;
            return true;
        } else {
            *out = true;
            return true;
        }
    }
    return false;
}

static bool opt__is_valid_value(OptValueType type, const char *value)
{
    switch (type) {
        case OPT_BOOL: { bool b;     return opt__parse_bool(value, &b); }
        case OPT_INT:  { int64_t i;  return opt__parse_int(value, &i); }
        case OPT_UINT: { uint64_t u; return opt__parse_uint(value, &u); }
        case OPT_STR:  return true;
    }
    return false;
}

// Letters are single bytes typed after '-': only printable ASCII, no space (locale independent)
static bool opt__is_printable_letter(char letter)
{
    unsigned char c = (unsigned char)letter;
    return c > ' ' && c <= '~';
}

static void opt__option_error(const OptOption *option, size_t index, const char *format, ...)
{
    if (option->name && option->name[0] != '\0')
        fprintf(stderr, "opt.h: Option \"%s\": ", option->name);
    else if (opt__is_printable_letter(option->letter))
        fprintf(stderr, "opt.h: Option '%c': ", option->letter);
    else
        fprintf(stderr, "opt.h: Option at index %zu: ", index);

    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static bool opt__validate_option_name(OptOption *option, size_t index)
{
    bool valid = true;
    if (option->name == NULL && option->letter == 0) {
        opt__option_error(option, index, "either a name or a letter must be provided");
        valid = false;
    }
    if (option->name) {
        if (option->name[0] == '\0') {
            opt__option_error(option, index, "name must not be empty, use NULL for an option without a name");
            valid = false;
        } else if (option->name[0] == '/' || option->name[0] == '-') {
            opt__option_error(option, index, "name must not start with '%c'", option->name[0]);
            valid = false;
        }
        if (option->name[strcspn(option->name, OPT__ASSIGNMENT_CHARS)] != '\0') {
            opt__option_error(option, index, "name must not contain any of \"%s\"", OPT__ASSIGNMENT_CHARS);
            valid = false;
        }
    }
    if (option->letter != 0) {
        if (!opt__is_printable_letter(option->letter)) {
            opt__option_error(option, index, "letter 0x%02X must be a printable ASCII character other than space",
                              (unsigned)(unsigned char)option->letter);
            valid = false;
        // '-' would start a long option, '/' is the ignore prefix, the others assign a value
        } else if (option->letter == '-' || option->letter == '/' || strchr(OPT__ASSIGNMENT_CHARS, option->letter)) {
            opt__option_error(option, index, "letter '%c' is reserved", option->letter);
            valid = false;
        }
    }
    return valid;
}

static bool opt__is_known_arity(OptArity arity)
{
    switch (arity) {
        case OPT_FLAG:
        case OPT_REQUIRED:
        case OPT_OPTIONAL:
        case OPT_COUNTER:
            return true;
    }
    return false;
}

static bool opt__is_known_type(OptValueType type)
{
    switch (type) {
        case OPT_BOOL:
        case OPT_STR:
        case OPT_INT:
        case OPT_UINT:
            return true;
    }
    return false;
}

static bool opt__validate_option_arity(OptOption *option, size_t index)
{
    bool valid = true;
    if (!opt__is_known_arity(option->arity)) {
        opt__option_error(option, index, "unknown arity %d", (int)option->arity);
        valid = false;
    }
    if (!opt__is_known_type(option->type)) {
        opt__option_error(option, index, "unknown type %d", (int)option->type);
        valid = false;
    }
    if (!valid) return false; // Combination checks below would be meaningless
    if (option->arity == OPT_FLAG && option->type != OPT_BOOL) {
        opt__option_error(option, index, "arity OPT_FLAG must take a reference of type OPT_BOOL");
        valid = false;
    }
    if (option->arity == OPT_OPTIONAL && option->type == OPT_BOOL) {
        opt__option_error(option, index, "arity OPT_OPTIONAL with type OPT_BOOL is not supported, use OPT_FLAG instead");
        valid = false;
    }
    if (option->arity == OPT_COUNTER && option->type != OPT_INT && option->type != OPT_UINT) {
        opt__option_error(option, index, "arity OPT_COUNTER must take an integral reference of type OPT_INT or OPT_UINT");
        valid = false;
    }
    return valid;
}

static bool opt__validate_option_implicit(OptOption *option, size_t index)
{
    if (option->implicit == NULL) return true;
    if (option->arity != OPT_OPTIONAL) {
        opt__option_error(option, index, "only an OPT_OPTIONAL option can have an implicit value");
        return false;
    }
    if (!opt__is_valid_value(option->type, option->implicit)) {
        opt__option_error(option, index, "implicit value \"%s\" is invalid for the option type", option->implicit);
        return false;
    }
    return true;
}

static bool opt__same_name(const OptOption *a, const OptOption *b)
{
    return a->name && b->name && strcmp(a->name, b->name) == 0;
}

static bool opt__same_letter(const OptOption *a, const OptOption *b)
{
    return a->letter != 0 && a->letter == b->letter;
}

static bool opt__validate_no_duplicate(OptOption *options, size_t option_count, size_t index)
{
    OptOption *option = &options[index];
    bool first_name = true, first_letter = true;
    for (size_t i = 0; i < index; i++) {
        if (opt__same_name(option, &options[i]))   first_name = false;
        if (opt__same_letter(option, &options[i])) first_letter = false;
    }
    bool dup_name = false, dup_letter = false;
    for (size_t i = index + 1; i < option_count; i++) {
        if (opt__same_name(option, &options[i]))   dup_name = true;
        if (opt__same_letter(option, &options[i])) dup_letter = true;
    }
    if (first_name && dup_name)
        fprintf(stderr, "opt.h: Two or more options share the same name \"%s\"\n", option->name);
    if (first_letter && dup_letter)
        fprintf(stderr, "opt.h: Two or more options share the same letter '%c'\n", option->letter);
    return first_name && first_letter && !dup_name && !dup_letter;
}

static bool opt__validate_options(OptOption *options, size_t option_count)
{
    if (options == NULL) {
        if (option_count == 0) return true; // No options: every option token is unknown
        fprintf(stderr, "opt.h: Options array is NULL but option count is %zu\n", option_count);
        return false;
    }
    bool valid = true;
    for (size_t i = 0; i < option_count; i++) {
        OptOption *option = &options[i];
        if (!opt__validate_option_name(option, i)) valid = false;
        if (!opt__validate_option_arity(option, i)) valid = false;
        else if (!opt__validate_option_implicit(option, i)) valid = false;
        if (!opt__validate_no_duplicate(options, option_count, i)) valid = false;
    }
    return valid;
}

OPT_DEF OptParser opt_init(int argc, char **argv, OptOption *options, size_t option_count)
{
    // Parser configuration is ill-formed, abort right-away
    bool valid = opt__validate_options(options, option_count);
    if (argc > 0 && argv == NULL) {
        fprintf(stderr, "opt.h: argv is NULL but argc is %d\n", argc);
        valid = false;
    }
    if (!valid) abort();

    // The same options array may be parsed more than once, so reset dynamic states
    for (size_t i = 0; i < option_count; i++) options[i].seen = false;

    return (OptParser) {
        .options = options,
        .option_count = option_count,
        .argc = argc > 0 ? argc : 0,
        .argv = argv,
        .index = argc > 0 ? 1 : 0,
    };
}

static bool opt__is(const OptToken *token, const OptOption *option)
{
    if (token->kind == OPT_TOKEN_SHORT) {
        return option->letter != 0
            && token->name_len == 1
            && token->name[0] == option->letter;
    }
    if (token->kind == OPT_TOKEN_LONG) {
        return option->name
            && strlen(option->name) == token->name_len
            && strncmp(token->name, option->name, token->name_len) == 0;
    }
    return false;
}

static OptOption* opt__find_option(OptParser *parser, OptToken *token)
{
    for (size_t i = 0; i < parser->option_count; i++) {
        OptOption *option = &parser->options[i];
        if (opt__is(token, option)) {
            return option;
        }
    }
    return NULL;
}

static OptTokenKind opt__option_kind(const char *text)
{
    if (strcmp(text, "--") == 0) return OPT_TOKEN_END_MARKER;
    if (strncmp(text, "--", 2) == 0) return OPT_TOKEN_LONG;
    if (strcmp(text, "-") == 0) return OPT_TOKEN_POSITIONAL;
    if (strncmp(text, "-", 1) == 0) return OPT_TOKEN_SHORT;
    return OPT_TOKEN_POSITIONAL;
}

static bool opt__should_store(const OptToken *token)
{
    return !token->ignored && token->option->ref != NULL;
}

static void opt__store_value(OptToken *token, const char *value)
{
    switch (token->option->type) {
        case OPT_BOOL: {
            bool b = true;
            if (value && !opt__parse_bool(value, &b)) token->error = OPT_ERROR_INVALID_VALUE;
            else if (opt__should_store(token)) *(bool*)token->option->ref = b;
        } break;
        case OPT_INT: {
            int64_t i = 0;
            if (!opt__parse_int(value, &i)) token->error = OPT_ERROR_INVALID_VALUE;
            else if (opt__should_store(token)) *(int64_t*)token->option->ref = i;
        } break;
        case OPT_UINT: {
            uint64_t i = 0;
            if (!opt__parse_uint(value, &i)) token->error = OPT_ERROR_INVALID_VALUE;
            else if (opt__should_store(token)) *(uint64_t*)token->option->ref = i;
        } break;
        case OPT_STR: {
            // ref may point to a char* as well as a const char*, so I can't just
            // cast ref into one or the other and assign it a value, that would
            // be undefined behaviour. But since they share the same bit representation
            // I can memcpy the address into the pointer
            if (opt__should_store(token)) memcpy(token->option->ref, &value, sizeof value);
        } break;
    }
}

static void opt__increment_value(OptToken *token)
{
    if (!opt__should_store(token)) return;
    if (token->option->type == OPT_INT) {
        int64_t *value = (int64_t*)token->option->ref;
        if (*value >= INT64_MAX) token->error = OPT_ERROR_INVALID_VALUE;
        else ++*value;
    }
    if (token->option->type == OPT_UINT) {
        uint64_t *value = (uint64_t*)token->option->ref;
        if (*value >= UINT64_MAX) token->error = OPT_ERROR_INVALID_VALUE;
        else ++*value;
    }
}

static void opt__extract_value(OptParser *parser, OptToken *token)
{
    switch (token->option->arity) {
        case OPT_FLAG: {
            opt__store_value(token, token->value);
        } break;
        case OPT_REQUIRED: {
            if (token->value == NULL) {
                if (parser->index < parser->argc) {
                    token->value = parser->argv[parser->index++];
                } else {
                    token->error = OPT_ERROR_MISSING_VALUE;
                    return;
                }
            }
            opt__store_value(token, token->value);
        } break;
        case OPT_OPTIONAL: {
            const char *value = token->value ? token->value : token->option->implicit;
            if (value) opt__store_value(token, value);
        } break;
        case OPT_COUNTER: {
            if (token->value) {
                opt__store_value(token, token->value);
            } else {
                opt__increment_value(token);
            }
        }
    }
}

static void opt__parse_long(OptToken *token, const char *arg)
{
    token->kind = OPT_TOKEN_LONG;
    token->ignored = strncmp(arg, "--/", 3) == 0;
    token->name = arg + (token->ignored ? 3 : 2);
    token->name_len = strcspn(token->name, OPT__ASSIGNMENT_CHARS);
    if (token->name[token->name_len] != '\0')
        token->value = token->name + token->name_len + 1;
}

static void opt__parse_short(OptParser *parser, OptToken *token)
{
    const char *letter = parser->cluster;
    token->kind = OPT_TOKEN_SHORT;
    token->ignored = parser->cluster_ignored;
    token->name = letter;
    if (letter[0] == '\0') { // bare "-/"
        token->name_len = 0;
        parser->cluster = NULL;
        return;
    }
    if (strchr(OPT__ASSIGNMENT_CHARS, letter[0])) { // "-=x", "-/=x": value without a letter
        token->name_len = 0;
        token->value = &letter[1];
        parser->cluster = NULL;
        return;
    }
    token->name_len = 1;
    if (letter[1] != '\0' && strchr(OPT__ASSIGNMENT_CHARS, letter[1])) {
        token->value = &letter[2];
        parser->cluster = NULL;
    } else {
        parser->cluster = letter[1] != '\0' ? &letter[1] : NULL;
    }
}

OPT_DEF bool opt_parse_next(OptParser *parser, OptToken *token)
{
    *token = (OptToken){0};

    if (parser->cluster) {
        token->raw_arg = parser->argv[parser->index - 1];
        opt__parse_short(parser, token);
    } else {
        if (parser->index >= parser->argc) return false;
        const char *arg = parser->argv[parser->index++];
        token->raw_arg = arg;
        token->name = arg;
        token->name_len = strlen(arg);
        token->kind = parser->stop_parsing_options ? OPT_TOKEN_POSITIONAL : opt__option_kind(arg);
        switch (token->kind) {
            case OPT_TOKEN_POSITIONAL:
                return true;
            case OPT_TOKEN_END_MARKER:
                parser->stop_parsing_options = true;
                return true;
            case OPT_TOKEN_LONG:
                opt__parse_long(token, arg);
                break;
            case OPT_TOKEN_SHORT:
                parser->cluster_ignored = strncmp(arg, "-/", 2) == 0;
                parser->cluster = arg + (parser->cluster_ignored ? 2 : 1);
                opt__parse_short(parser, token);
                break;
        }
    }

    if (token->name_len == 0) {
        token->error = OPT_ERROR_MISSING_NAME;
        parser->cluster = NULL;
        return true;
    }

    token->option = opt__find_option(parser, token);
    if (token->option == NULL) {
        token->error = OPT_ERROR_UNKNOWN_OPTION;
        parser->cluster = NULL;
        return true;
    }
    // "-fbar.txt" / "-cfbar.txt" / "-j4": the rest of the cluster is the value.
    // Counters are excluded since digits may be option letters ("-v4" is "-v -4"); use "-v=3".
    bool takes_attached_value = token->option->arity == OPT_REQUIRED || token->option->arity == OPT_OPTIONAL;
    if (takes_attached_value && !token->value && parser->cluster) {
        token->value = parser->cluster;
        parser->cluster = NULL;
    }
    opt__extract_value(parser, token);
    if (!token->ignored && token->error == OPT_ERROR_NONE) token->option->seen = true;
    return true;
}

static bool opt__was_seen(OptParser *parser, const char *name, char letter)
{
    for (size_t i = 0; i < parser->option_count; i++) {
        OptOption *option = &parser->options[i];
        if (name && option->name && strcmp(name, option->name) == 0) return option->seen;
        if (letter && letter == option->letter) return option->seen;
    }
    return false;
}

OPT_DEF bool opt_was_seen_name(OptParser *parser, const char *name)
{
    return opt__was_seen(parser, name, 0);
}

OPT_DEF bool opt_was_seen_letter(OptParser *parser, char letter)
{
    return opt__was_seen(parser, NULL, letter);
}

#endif // OPT_IMPLEMENTATION

#ifdef OPT_TESTING

#include <inttypes.h>
#include <stdio.h>

// Name and letter are both optional: print null for a missing one
void print_option(OptOption *option)
{
    if (!option) {
        printf("null");
        return;
    }
    if (option->name) printf("\"%s\"", option->name);
    else printf("null");
    if (option->letter) printf(" '%c'", option->letter);
    else printf(" null");
}

void print_value(OptOption *option)
{
    if (!option || !option->ref) {
        printf("null");
        return;
    }
    switch (option->type) {
        case OPT_BOOL: printf("%s", *(bool*)option->ref ? "true" : "false");
            break;
        case OPT_INT:  printf("%"PRIi64, *(int64_t*)option->ref);
            break;
        case OPT_UINT: printf("%"PRIu64, *(uint64_t*)option->ref);
            break;
        case OPT_STR: {
            const char *str = *(const char**)option->ref;
            if (str) printf("\"%s\"", str);
            else     printf("null");
        } break;
    }
}

int main(int argc, char **argv)
{
    uint64_t jobs = 0;
    bool create = false;
    bool gzip = false;
    int64_t verbose = 0;
    const char *archive_filename = NULL;
    const char *change_to_directory = NULL;

    OptOption options[] = {
        {"jobs",      'j', OPT_OPTIONAL, OPT_REF(&jobs), .implicit = "8"},
        {"create",    'c', OPT_FLAG,     OPT_REF(&create)},
        {"gzip",      'z', OPT_FLAG,     OPT_REF(&gzip)},
        {"verbose",   'v', OPT_COUNTER,  OPT_REF(&verbose)},
        {"file",      'f', OPT_REQUIRED, OPT_REF(&archive_filename)},
        {"directory", 'C', OPT_REQUIRED, OPT_REF(&change_to_directory)},
    };

    OptParser parser = opt_init(argc, argv, options, OPT_ARRAY_LEN(options));

    OptToken token;
    while (opt_parse_next(&parser, &token)) {
        printf("New arg: %.*s\n", (int)token.name_len, token.name);
        switch(token.kind) {
            case OPT_TOKEN_POSITIONAL:
                printf("  Adding file %s to archive\n", token.name);
                break;
            case OPT_TOKEN_END_MARKER:
                printf("  End of options, remaining arguments are files\n");
                break;
            case OPT_TOKEN_SHORT:
            case OPT_TOKEN_LONG:
                printf("  Got option: %.*s\n", (int)token.name_len, token.name);
                printf("    ignored: %s\n", token.ignored ? "yes" : "no");
                printf("    kind: %s\n", opt_token_kind_to_str(token.kind));
                printf("    related option: "); print_option(token.option); printf("\n");
                printf("    value: "); print_value(token.option); printf("\n");
                printf("    error: %s\n", opt_error_to_str(token.error));
                break;
        }
    }

    printf("Summary:\n");
    for (size_t i = 0; i < OPT_ARRAY_LEN(options); i++) {
        OptOption *option = &options[i];
        printf("  option: "); print_option(option);
        printf(", value: "); print_value(option); printf("\n");
    }
}

#endif // OPT_TESTING

#endif // OPT_H_
