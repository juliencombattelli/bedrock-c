# To Do

## opt.h

### Must do

#### High value

- [-] *WIP* Port the original opt1/opt2 test suites to opt.h
- [ ] Negatable flags: accept `--no-<name>` for `OPT_FLAG` as `--<name>=false`
- [ ] "Did you mean" suggestions for unknown options (Levenshtein over option names)

#### Medium value

- [ ] More value types:
  * [ ] `OPT_DOUBLE` (`strtod`, same whitespace/errno handling)
  * [ ] `OPT_CHOICE` (list of allowed strings, stores the index; usable in help output)
  * [ ] `OPT_CUSTOM` (`bool (*parse)(const char *value, void *ref)` callback)
- [ ] Telling an explicit value from the implicit one. After `--jobs`, you can't tell whether `jobs == 8` came from the user or from implicit. A field like `bool used_implicit` on `OptToken` would fix that.

#### Smaller things / possible issues

- [ ] `seen` is a public output field: document it as "output, reset by `opt_init`" or move it out of the option table

### May do, maybe not

- [ ] Help/usage generation: add `help` and `value_name` fields to `OptOption`, and `opt_print_help(FILE*, const OptOption*, size_t)` (show `[=N]` for `OPT_OPTIONAL` and the implicit value)
- [ ] Error formatter: `opt_format_error(char *buf, size_t size, const OptToken *token)` producing messages like "unknown option '--fiel'", "option '-f' requires a value", "invalid value 'abc' for '--jobs'"
- [ ] Opt-in unambiguous long-prefix matching (`--verb` -> `--verbose`) + `OPT_ERROR_AMBIGUOUS_OPTION`
- [ ] C++/linkage: `extern "C"` guards, document that `OPT_REF` (`_Generic`) and designated initializers are C-only, suggest `#define OPT_DEF static`
=> extern "C" can be done on the C++ side when including opt.h
=> _Generic could be conditionally defined only for C

### Won't do

- [x] Mandatory options: `required` flag on `OptOption` + `opt_first_missing(OptParser*)` checked after parsing (reuses `seen`) => a required option is by definition not an option and should be provided as a positional argument
