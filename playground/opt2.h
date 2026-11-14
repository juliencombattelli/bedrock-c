#ifndef OPT_H_
#define OPT_H_

////////////////////////////////////////////////////////////////////////////////
///                          PUBLIC IMPLEMENTATION                           ///
////////////////////////////////////////////////////////////////////////////////

#ifndef OPT_DEF
#define OPT_DEF
#endif

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

///=============================================================================
/// Configuration
///=============================================================================

#ifndef OPT_SCHEME_LIST_MAX_LEN
#define OPT_SCHEME_LIST_MAX_LEN 8
#endif

#define OPT_ARRAY_LEN(array) (sizeof(array) / sizeof((array)[0]))

typedef struct {
    const char *tokens[OPT_SCHEME_LIST_MAX_LEN];
    bool first_positional;
} OptStopCondition;

typedef struct {
    const char *prefix;
    const char *ignore_seq; // Immediately after the prefix: parse but ignore the option and its value if any
} OptPrefix;

typedef struct {
    OptPrefix prefixes[OPT_SCHEME_LIST_MAX_LEN];
    const char *assignments[OPT_SCHEME_LIST_MAX_LEN];
    bool allow_collating;
    bool allow_clustering;
} OptSchemeShort;

typedef struct {
    OptPrefix prefixes[OPT_SCHEME_LIST_MAX_LEN];
    const char *assignments[OPT_SCHEME_LIST_MAX_LEN];
} OptSchemeLong;

typedef struct {
    OptSchemeShort short_config;
    OptSchemeLong long_config;
    OptStopCondition stop_on;
} OptScheme;

OPT_DEF OptScheme opt_scheme_builtin(void);
OPT_DEF OptScheme opt_scheme_gnu(void);

typedef enum {
    OPT_NO_VALUE,
    OPT_REQUIRED,
    OPT_OPTIONAL,
} OptArity;

typedef enum {
    OPT_STRING,
    OPT_INT,
} OptValueType;

typedef struct {
    const char *name; // Long option name
    char letter; // Short option name
    OptArity arity;
    OptValueType type;
    void *ref;
} OptOption;

typedef struct {
    const char *parser_name;
    OptScheme scheme;
    OptOption *options;
    size_t option_count;
} OptConfig;

///=============================================================================
/// Parser
///=============================================================================

typedef enum {
    OPT_TOKEN_POSITIONAL,
    OPT_TOKEN_SHORT,
    OPT_TOKEN_LONG,
    OPT_TOKEN_VALUE,
    OPT_TOKEN_END_MARKER,
} OptTokenKind;

OPT_DEF const char* opt_token_kind_to_str(OptTokenKind token_kind);

typedef enum {
    OPT_ERROR_NONE,
    OPT_ERROR_UNKNOWN_OPTION,
    OPT_ERROR_MISSING_VALUE,
    OPT_ERROR_UNEXPECTED_VALUE,
    OPT_ERROR_INVALID_CLUSTER,
    OPT_ERROR_INVALID_VALUE,
} OptError;

OPT_DEF const char* opt_error_to_str(OptError error);

typedef struct {
    OptTokenKind kind;
    const char *raw_arg;
    OptError error;
    const OptOption *option; // Reserved for schema-based parsing; NULL in the iterator API
    const char *name;  // Parsed name, borrowed from raw_arg (not NUL-terminated, see name_len)
    size_t name_len;
    const char *value; // Explicit inline value; helpers return consumed values directly
    bool ignored;     // Switch matched an ignore sequence; caller suppresses its effects
} OptToken;

typedef struct {
    int argc;
    const char **argv;
    int index;     // Next argv entry, after any explicitly consumed value.
    int sub_index; // Offset of the next short name within the pending cluster; zero when none.
    bool stop_parsing_options;
    const char *cluster_raw;  // argv entry of the pending short-option cluster; NULL when none
    const char *cluster_body; // Cluster names after prefix and ignore sequence
    bool cluster_ignored;
} OptStream;


typedef struct {
    OptScheme scheme;
    OptStream stream;      // argv, index, sub_index, cluster state, stop flag
    const char *pending;   // Explicit inline value not yet consumed (e.g. --directory=project)
    OptError error;
    const char *error_arg;
    const char *current_arg; // Current option/value's argv entry, for diagnostics
    bool current_ignored;
} OptParser;

OPT_DEF OptParser opt_parser_init(OptScheme scheme, int argc, const char **argv);

// Returns false at the end of argv or once an error has been recorded.
// Unconsumed explicit inline values are emitted as OPT_TOKEN_VALUE.
OPT_DEF bool opt_next(OptParser *p, OptToken *token);

// Short char (0 = none) or long name (NULL = none). Odin's "-o" is opt_is(&t, 0, "o").
OPT_DEF bool opt_is(const OptToken *token, char short_name, const char *long_name);

// Required value: inline value or rest of the cluster (when collating is enabled),
// otherwise the next argv entry.
// Returns NULL and records OPT_ERROR_MISSING_VALUE if none.
OPT_DEF const char *opt_value(OptParser *p);
// Optional value: inline or collated only; never consumes the next entry. NULL if absent.
OPT_DEF const char *opt_optional_value(OptParser *p);
// opt_value + strtol; records OPT_ERROR_INVALID_VALUE on failure, leaving *out untouched.
OPT_DEF bool opt_value_int(OptParser *p, int *out);
// Optional integer: inline/collated, otherwise consumes the next entry only if valid.
// Returns false if absent (no error) or invalid (inline/collated values record an error).
// Leaves *out untouched on false; a null output pointer records OPT_ERROR_INVALID_VALUE.
OPT_DEF bool opt_optional_value_int(OptParser *p, int *out);

OPT_DEF void opt_unexpected(OptParser *p, const OptToken *token); // OPT_ERROR_UNKNOWN_OPTION
// Records OPT_ERROR_INVALID_VALUE and prints the supplied diagnostic to stderr.
OPT_DEF void opt_fail(OptParser *p, const OptToken *token, const char *reason);

////////////////////////////////////////////////////////////////////////////////
///                          PRIVATE IMPLEMENTATION                          ///
///              Internals below -- not part of the public API               ///
///                    Content may change without notice                     ///
///                                                                          ///
/// Define the macro OPT_IMPLEMENTATION in one (and only one) of your        ///
/// compiled files before including this header.                             ///
////////////////////////////////////////////////////////////////////////////////
#ifdef OPT_IMPLEMENTATION

///=============================================================================
/// Helpers
///=============================================================================

static const OptScheme opt__scheme_builtin = {
    .short_config = {
        .prefixes = {{.prefix = "-", .ignore_seq = "/"}},
        .assignments = {"=", ":", " "},
        .allow_collating = true,
        .allow_clustering = true,
    },
    .long_config = {
        .prefixes = {{.prefix = "--", .ignore_seq = "/"}},
        .assignments = {"=", ":", " "},
    },
    .stop_on.tokens = {"--"},
};

OPT_DEF OptScheme opt_scheme_builtin(void)
{
    return opt__scheme_builtin;
}

static const OptScheme opt__scheme_gnu = {
    .short_config = {
        .prefixes = {{.prefix = "-"}},
        .assignments = {"=", " "},
        .allow_collating = true,
        .allow_clustering = true,
    },
    .long_config = {
        .prefixes = {{.prefix = "--"}},
        .assignments = {"=", " "},
    },
    .stop_on.tokens = {"--"},
};

OPT_DEF OptScheme opt_scheme_gnu(void)
{
    return opt__scheme_gnu;
}

static const char *opt__validate_scheme(const OptScheme *scheme)
{
    const OptPrefix *prefixes[] = {
        scheme->short_config.prefixes, scheme->long_config.prefixes,
    };
    const char *const *strings[] = {
        scheme->short_config.assignments, scheme->long_config.assignments,
        scheme->stop_on.tokens,
    };
    for (size_t kind = 0; kind < OPT_ARRAY_LEN(prefixes); ++kind) {
        for (size_t i = 0; i < OPT_SCHEME_LIST_MAX_LEN && prefixes[kind][i].prefix; ++i) {
            const OptPrefix *prefix = &prefixes[kind][i];
            if (!prefix->prefix[0]) return "option prefixes must not be empty";
            for (size_t j = 0; j < i; ++j) {
                const OptPrefix *other = &prefixes[kind][j];
                if (strcmp(prefix->prefix, other->prefix) == 0 &&
                    strcmp(prefix->ignore_seq ? prefix->ignore_seq : "",
                           other->ignore_seq ? other->ignore_seq : "") != 0) {
                    return "duplicate prefixes have conflicting ignore sequences";
                }
            }
            if (kind == 1) {
                for (size_t j = 0; j < OPT_SCHEME_LIST_MAX_LEN && prefixes[0][j].prefix; ++j) {
                    if (strcmp(prefix->prefix, prefixes[0][j].prefix) == 0) {
                        return "short and long prefixes must be distinct";
                    }
                }
            }
        }
    }
    for (size_t list = 0; list < OPT_ARRAY_LEN(strings); ++list) {
        for (size_t i = 0; i < OPT_SCHEME_LIST_MAX_LEN && strings[list][i]; ++i) {
            if (!strings[list][i][0]) return "assignments and stop tokens must not be empty";
        }
    }
    return NULL;
}

static void opt__set_error(OptParser *p, OptError error, const char *arg)
{
    if (p->error != OPT_ERROR_NONE) return;
    p->error = error;
    p->error_arg = arg;
}

static size_t opt__match_assignment(const char *text, const char *const *assignments)
{
    size_t longest = 0;
    for (size_t i = 0; i < OPT_SCHEME_LIST_MAX_LEN && assignments[i]; ++i) {
        size_t length = strlen(assignments[i]);
        if (length > longest && strncmp(text, assignments[i], length) == 0) {
            longest = length;
        }
    }
    return longest;
}

static void opt__clear_cluster(OptParser *p)
{
    p->stream.cluster_raw = NULL;
    p->stream.cluster_body = NULL;
    p->stream.sub_index = 0;
    p->stream.cluster_ignored = false;
}

static void opt__next_short(OptParser *p, OptToken *token)
{
    const char *name = p->stream.cluster_body + p->stream.sub_index;
    *token = (OptToken){
        .kind = OPT_TOKEN_SHORT,
        .raw_arg = p->stream.cluster_raw,
        .name = name,
        .name_len = 1,
        .ignored = p->stream.cluster_ignored,
    };
    size_t assignment = opt__match_assignment(name + 1, p->scheme.short_config.assignments);
    if (assignment) {
        p->pending = name + 1 + assignment;
        token->value = p->pending;
        opt__clear_cluster(p);
    } else if (!name[1]) {
        opt__clear_cluster(p);
    } else {
        ++p->stream.sub_index;
    }
    p->current_arg = token->raw_arg;
    p->current_ignored = token->ignored;
}

///=============================================================================
/// Parser
///=============================================================================

OPT_DEF const char* opt_token_kind_to_str(OptTokenKind token_kind)
{
    switch (token_kind) {
        case OPT_TOKEN_POSITIONAL:  return "positional";
        case OPT_TOKEN_SHORT:       return "short";
        case OPT_TOKEN_LONG:        return "long";
        case OPT_TOKEN_VALUE:       return "value";
        case OPT_TOKEN_END_MARKER:  return "end-marker";
        default: return NULL;
    }
}

OPT_DEF const char* opt_error_to_str(OptError error)
{
    switch (error) {
        case OPT_ERROR_NONE:                return "no error";
        case OPT_ERROR_UNKNOWN_OPTION:      return "unknown option";
        case OPT_ERROR_MISSING_VALUE:       return "missing option value";
        case OPT_ERROR_UNEXPECTED_VALUE:    return "option does not accept a value";
        case OPT_ERROR_INVALID_CLUSTER:     return "short-option clustering is disabled";
        case OPT_ERROR_INVALID_VALUE:       return "invalid option value";
        default:                            return NULL;
    }
}

OPT_DEF OptParser opt_parser_init(OptScheme scheme, int argc, const char **argv)
{
    OptParser p = {
        .scheme = scheme,
        .stream = {.argc = argc, .argv = argv, .index = argc > 0 ? 1 : 0},
    };
    const char *reason = opt__validate_scheme(&scheme);
    if (reason) {
        opt__set_error(&p, OPT_ERROR_INVALID_VALUE, NULL);
        fprintf(stderr, "opt.h: invalid scheme: %s\n", reason);
        return p;
    }
    if (argc < 0 || (argc > 0 && !argv)) {
        opt__set_error(&p, OPT_ERROR_INVALID_VALUE, NULL);
        fprintf(stderr, "opt.h: invalid argument vector\n");
        return p;
    }
    for (int i = 0; i < argc; ++i) {
        if (!argv[i]) {
            opt__set_error(&p, OPT_ERROR_INVALID_VALUE, NULL);
            fprintf(stderr, "opt.h: argv[%d] must not be NULL\n", i);
            break;
        }
    }
    return p;
}

OPT_DEF bool opt_next(OptParser *p, OptToken *token)
{
    *token = (OptToken){0};
    if (p->error != OPT_ERROR_NONE) {
        token->error = p->error;
        token->raw_arg = p->error_arg;
        return false;
    }
    if (p->pending) {
        *token = (OptToken){
            .kind = OPT_TOKEN_VALUE,
            .raw_arg = p->current_arg,
            .value = p->pending,
            .ignored = p->current_ignored,
        };
        p->pending = NULL;
        return true;
    }
    if (p->stream.cluster_body) {
        if (!p->scheme.short_config.allow_clustering) {
            opt__set_error(p, OPT_ERROR_INVALID_CLUSTER, p->stream.cluster_raw);
            token->error = p->error;
            token->raw_arg = p->error_arg;
            return false;
        }
        opt__next_short(p, token);
        return true;
    }
    if (p->stream.index >= p->stream.argc) return false;

    const char *arg = p->stream.argv[p->stream.index++];
    p->current_arg = arg;
    p->current_ignored = false;
    *token = (OptToken){.kind = OPT_TOKEN_POSITIONAL, .raw_arg = arg};
    if (p->stream.stop_parsing_options) return true;

    for (size_t i = 0; i < OPT_SCHEME_LIST_MAX_LEN && p->scheme.stop_on.tokens[i]; ++i) {
        if (strcmp(arg, p->scheme.stop_on.tokens[i]) == 0) {
            token->kind = OPT_TOKEN_END_MARKER;
            p->stream.stop_parsing_options = true;
            return true;
        }
    }

    const OptPrefix *prefix = NULL;
    size_t prefix_length = 0;
    bool is_long = false;
    const OptPrefix *lists[] = {p->scheme.short_config.prefixes, p->scheme.long_config.prefixes};
    for (size_t kind = 0; kind < OPT_ARRAY_LEN(lists); ++kind) {
        for (size_t i = 0; i < OPT_SCHEME_LIST_MAX_LEN && lists[kind][i].prefix; ++i) {
            size_t length = strlen(lists[kind][i].prefix);
            if (length > prefix_length && strncmp(arg, lists[kind][i].prefix, length) == 0) {
                prefix = &lists[kind][i];
                prefix_length = length;
                is_long = kind == 1;
            }
        }
    }
    if (!prefix || !arg[prefix_length]) {
        if (p->scheme.stop_on.first_positional) p->stream.stop_parsing_options = true;
        return true;
    }

    const char *body = arg + prefix_length;
    if (prefix->ignore_seq && prefix->ignore_seq[0]) {
        size_t length = strlen(prefix->ignore_seq);
        if (strncmp(body, prefix->ignore_seq, length) == 0) {
            body += length;
            token->ignored = true;
        }
    }
    p->current_ignored = token->ignored;
    if (!body[0]) {
        opt__set_error(p, OPT_ERROR_UNKNOWN_OPTION, arg);
        token->error = p->error;
        return false;
    }
    if (!is_long) {
        p->stream.cluster_raw = arg;
        p->stream.cluster_body = body;
        p->stream.cluster_ignored = token->ignored;
        opt__next_short(p, token);
        return true;
    }

    token->kind = OPT_TOKEN_LONG;
    token->name = body;
    const char *end = body;
    while (*end) {
        size_t assignment = opt__match_assignment(end, p->scheme.long_config.assignments);
        if (assignment) {
            p->pending = end + assignment;
            token->value = p->pending;
            break;
        }
        ++end;
    }
    token->name_len = (size_t)(end - body);
    if (!token->name_len) {
        opt__set_error(p, OPT_ERROR_UNKNOWN_OPTION, arg);
        token->error = p->error;
        return false;
    }
    return true;
}

OPT_DEF bool opt_is(const OptToken *token, char short_name, const char *long_name)
{
    if (token->kind == OPT_TOKEN_SHORT) {
        return short_name != 0 && token->name_len == 1 && token->name[0] == short_name;
    }
    if (token->kind == OPT_TOKEN_LONG) {
        return long_name && strlen(long_name) == token->name_len &&
               strncmp(token->name, long_name, token->name_len) == 0;
    }
    return false;
}

OPT_DEF const char *opt_optional_value(OptParser *p)
{
    if (p->error != OPT_ERROR_NONE) return NULL;
    if (p->pending) {
        const char *value = p->pending;
        p->pending = NULL;
        return value;
    }
    if (p->scheme.short_config.allow_collating && p->stream.cluster_body) {
        const char *value = p->stream.cluster_body + p->stream.sub_index;
        opt__clear_cluster(p);
        return value;
    }
    return NULL;
}

OPT_DEF const char *opt_value(OptParser *p)
{
    if (p->error != OPT_ERROR_NONE) return NULL;
    const char *value = opt_optional_value(p);
    if (value) return value;
    if (p->stream.index < p->stream.argc) return p->stream.argv[p->stream.index++];
    opt__set_error(p, OPT_ERROR_MISSING_VALUE, p->current_arg);
    return NULL;
}

static bool opt__parse_int(const char *value, int *out)
{
    char *end = NULL;
    errno = 0;
    long number = strtol(value, &end, 10);
    if (end == value || *end != '\0' || errno == ERANGE || number < INT_MIN || number > INT_MAX)
        return false;
    *out = (int)number;
    return true;
}

OPT_DEF bool opt_value_int(OptParser *p, int *out)
{
    if (p->error != OPT_ERROR_NONE) return false;
    if (!out) {
        opt__set_error(p, OPT_ERROR_INVALID_VALUE, p->current_arg);
        return false;
    }
    const char *value = opt_value(p);
    if (!value) return false;
    if (!opt__parse_int(value, out)) {
        opt__set_error(p, OPT_ERROR_INVALID_VALUE, p->current_arg);
        return false;
    }
    return true;
}

OPT_DEF bool opt_optional_value_int(OptParser *p, int *out)
{
    if (p->error != OPT_ERROR_NONE) return false;
    if (!out) {
        opt__set_error(p, OPT_ERROR_INVALID_VALUE, p->current_arg);
        return false;
    }
    const char *value = opt_optional_value(p);
    if (value) {
        if (!opt__parse_int(value, out)) {
            opt__set_error(p, OPT_ERROR_INVALID_VALUE, p->current_arg);
            return false;
        }
        return true;
    }
    if (p->stream.index >= p->stream.argc ||
        !opt__parse_int(p->stream.argv[p->stream.index], out))
        return false;
    ++p->stream.index;
    return true;
}

OPT_DEF void opt_unexpected(OptParser *p, const OptToken *token)
{
    opt__set_error(p, OPT_ERROR_UNKNOWN_OPTION, token->raw_arg);
}

OPT_DEF void opt_fail(OptParser *p, const OptToken *token, const char *reason)
{
    if (p->error != OPT_ERROR_NONE) return;
    opt__set_error(p, OPT_ERROR_INVALID_VALUE, token->raw_arg);
    fprintf(stderr, "opt.h: %s: %s\n", token->raw_arg ? token->raw_arg : "(argument)",
            reason ? reason : opt_error_to_str(p->error));
}

#endif // OPT_IMPLEMENTATION

#endif // OPT_H_
