/*
 * cpp.pre.cpp: [cpp.pre] Preamble: the groups of a preprocessing file,
 * where its preprocessing tokens come from, and which ones begin a
 * directive.  Also [cpp.null], too small for a file of its own.
 *
 *     preprocessing-file:
 *         group_opt
 *         module-file
 *
 *     group-part:
 *         control-line
 *         if-section
 *         text-line
 *         '#' conditionally-supported-directive
 *
 * [cpp.pre]/6: "The implementation can process and skip sections of source
 * files conditionally, include other source files, import macros from header
 * units, and replace macros.  These capabilities are called preprocessing".
 * Phase 4 reads the preprocessing tokens of phase 3 ([lex.phases]/1.4) one
 * at a time: preprocessor::next() returns the tokens of text-lines, macros
 * replaced, and executes the directives it meets on the way.
 */
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

#include "../cplusplus-internal.h"

namespace cplusplus
{

preprocessor::preprocessor(source_files &f, const preprocessor_options &o)
    : files(f), options(o), behavior(f.behavior), diagnostics(f.diagnostics),
      condition_behavior(f.behavior),
      condition_lexer(condition_file, condition_behavior, f.diagnostics)
{
    /* [cpp.cond]/12: "all signed and unsigned integer types act as if they
       have the same representation as, respectively, intmax_t or
       uintmax_t": a literal's type is found as if every integer type were
       64 bits wide */
    condition_behavior.int_width = condition_behavior.long_width =
        condition_behavior.long_long_width = 64;
    condition_file.from_memory("", "", 0);
    condition_lexer.files = &files;
}

/* [cpp.pre]: the directive names "appearing after a # in the syntax";
   elifdef, elifndef and warning are C++23's (P2334R1, P2437R1), whose use
   in earlier editions clang accepts with a warning
   ([ext.clang] clang_later_edition_features, directive_elif_else) */
directive_kind find_directive(const implementation_defined_behavior &b,
                              const char *s, size_t n)
{
    static constexpr struct {
        const char *name;
        directive_kind kind;
    } names[] = {
        {"include", directive_kind::include},
        {"define", directive_kind::define},
        {"undef", directive_kind::undef},
        {"line", directive_kind::line},
        {"error", directive_kind::error},
        {"warning", directive_kind::warning},
        {"pragma", directive_kind::pragma},
        {"if", directive_kind::if_},
        {"ifdef", directive_kind::ifdef},
        {"ifndef", directive_kind::ifndef},
        {"elif", directive_kind::elif},
        {"elifdef", directive_kind::elifdef},
        {"elifndef", directive_kind::elifndef},
        {"else", directive_kind::else_},
        {"endif", directive_kind::endif},
    };
    for (const auto &d : names)
        if (strlen(d.name) == n && !memcmp(d.name, s, n))
            return d.kind;
    /* [cpp.pre]/2: conditionally-supported-directives, the extensions'
       (ext/) */
    static constexpr struct {
        const char *name;
        directive_kind kind;
        extension e;
    } extended[] = {
        {"include_next", directive_kind::include_next,
         extension::gnu_include_next},
        {"ident", directive_kind::ident, extension::gnu_ident_directive},
        {"sccs", directive_kind::ident, extension::gnu_ident_directive},
        {"import", directive_kind::import, extension::clang_import_directive},
        {"embed", directive_kind::embed, extension::clang_embed},
    };
    for (const auto &d : extended)
        if (strlen(d.name) == n && !memcmp(d.name, s, n) && b.has(d.e))
            return d.kind;
    return directive_kind::unknown;
}

/* the text of a file of directives (the predefined macros, the command
   line's) executed before the main file: it has no text-lines */
void preprocessor::execute_definitions(source_files::entry &file)
{
    inclusions.clear();
    inclusions.append({&file, -1, 0, 0, false, false, {}});
    for (preprocessing_token t = next();
         t.kind != preprocessing_token_kind::end_of_file; t = next())
        ;
    inclusions.clear();
    contexts.clear();
    context_tokens.clear();
}

bool preprocessor::system_header_at(source_location location) const
{
    const source_files::entry *e = files.find(location);
    return e && e->system_header;
}

bool preprocessor::start(const char *path)
{
    define_dynamic_macros();
    /* [cpp.predefined]/1-2 and clang's own, as clang's "<built-in>" file */
    owned_buffer text;
    write_predefined_macros(text.get());
    source_files::entry &built_in =
        files.add_text("<built-in>", text.data(), text.size(), false);
    predefines_file = built_in.index;
    execute_definitions(built_in);
    if (!options.command_line.empty())
        execute_definitions(files.add_text("<command line>",
                                           options.command_line.data(),
                                           options.command_line.size(), false));
    main_file = files.open(path);
    if (!main_file)
        return false;
    struct stat st;
    if (stat(path, &st) == 0)
        entered_files.append({(uint64_t)st.st_dev, (uint64_t)st.st_ino});
    inclusions.append({main_file, -1, 0, 0, false, false, {}});

    /* [cpp.predefined]/1: "The date of translation of the source file" */
    time_t now = ::time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    static constexpr const char *months[] = {"Jan", "Feb", "Mar", "Apr",
                                             "May", "Jun", "Jul", "Aug",
                                             "Sep", "Oct", "Nov", "Dec"};
    snprintf(date, sizeof date, "\"%s %2d %d\"", months[local.tm_mon],
             local.tm_mday, local.tm_year + 1900);
    snprintf(time_of_day, sizeof time_of_day, "\"%02d:%02d:%02d\"",
             local.tm_hour, local.tm_min, local.tm_sec);
    started = true;
    return true;
}

/*
 * [cpp.pre]/1: "At the start of translation phase 4, the first token in the
 * sequence, referred to as a directive-introducing token, begins with the
 * first character in the source file (optionally after white space
 * containing no new-line characters) or follows white space containing at
 * least one new-line character, and is a # preprocessing token".  Only a
 * token read from a source file can be one ([cpp.rescan]/4: a replacement
 * "is not processed as a preprocessing directive even if it resembles one").
 */
bool preprocessor::at_directive(const preprocessing_token &t)
{
    return t.at_start_of_line && is_operator(t, preprocessing_op_or_punc::HASH);
}

/*
 * The next preprocessing token of the current source file, past the
 * directives, which are executed, and the groups they skip; at the end of
 * an included file, the includer's next ([cpp.include]/2: the directive is
 * replaced "by the entire contents of the header").
 */
preprocessing_token preprocessor::read_file_token(bool header_name_permitted)
{
    for (;;) {
        inclusion &in = inclusions.back();
        preprocessing_token t;
        if (in.has_lookahead) {
            t = in.lookahead;
            in.has_lookahead = false;
        } else
            t = files.next(*in.file, header_name_permitted);
        last_file_token = t;
        if (at_directive(t)) {
            /* [cpp.replace]/13: "If there are sequences of preprocessing
               tokens within the list of arguments that would otherwise act
               as preprocessing directives, the behavior is undefined"; as
               [impl.clang] they act as directives */
            if (collecting_arguments)
                diagnostics.issue(diagnostic_id::ext_embedded_directive,
                                  t.location);
            size_t depth = contexts.size();
            execute_directive(t);
            /* a directive that made tokens (#embed): they are read first */
            if (contexts.size() > depth) {
                preprocessing_token none;
                none.kind = preprocessing_token_kind::placemarker;
                return none;
            }
            continue;
        }
        if (t.kind == preprocessing_token_kind::end_of_file) {
            if (stop_at_file_end && inclusions.size() > 1) {
                unread_file_token(t);
                return t;
            }
            source_files::entry *left = in.file;
            leave_file();
            if (!inclusions.empty())
                continue;
            if (left == main_file)
                warn_unused_macros();
            /* the end of the main file stays the end */
            inclusions.append({main_file, -1, 0, 0, false, true, t});
        }
        return t;
    }
}

void preprocessor::unread_file_token(const preprocessing_token &t)
{
    inclusion &in = inclusions.back();
    in.lookahead = t;
    in.has_lookahead = true;
}

/* the end of a source file: [cpp.pre]'s if-section ends with its endif-line
   in the same file */
void preprocessor::leave_file()
{
    inclusion &in = inclusions.back();
    while (conditionals.size() > in.conditional_depth) {
        diagnostics.issue(diagnostic_id::err_pp_unterminated_conditional,
                          conditionals[conditionals.size() - 1].location);
        conditionals.resize(conditionals.size() - 1);
    }
    inclusions.resize(inclusions.size() - 1);
}

/*
 * The next preprocessing token of the directive being executed, or its end:
 * [cpp.pre]/1 "The last token in the sequence is the first token within the
 * sequence that is immediately followed by whitespace containing a new-line
 * character."  The token after the end is kept for read_file_token.
 */
preprocessing_token preprocessor::directive_token(bool header_name_permitted)
{
    inclusion &in = inclusions.back();
    preprocessing_token t;
    if (in.has_lookahead) {
        t = in.lookahead;
        in.has_lookahead = false;
    } else
        t = files.next(*in.file, header_name_permitted);
    if (t.at_start_of_line || t.kind == preprocessing_token_kind::end_of_file) {
        in.lookahead = t;
        in.has_lookahead = true;
        preprocessing_token end;
        end.kind = preprocessing_token_kind::end_of_file;
        end.location = directive_end;
        return end;
    }
    directive_end = t.location + t.extent;
    return t;
}

void preprocessor::skip_rest_of_directive()
{
    while (!is_end(directive_token()))
        ;
}

/* footnote 2 of [cpp.cond]: "a preprocessing token shall not follow a #else
   or #endif directive before the terminating new-line character"; [impl.clang] clang
   warns of the extra tokens there and after other directives' operands */
void preprocessor::expect_end_of_directive(const char *directive)
{
    preprocessing_token t = directive_token();
    if (is_end(t))
        return;
    diagnostics.issue(diagnostic_id::ext_pp_extra_tokens_at_eol, t.location)
        << directive;
    skip_rest_of_directive();
}

/*
 *     control-line:
 *         '# include' pp-tokens new-line
 *         ...
 *         '#' new-line
 *
 * [cpp.replace]/9: "If a # preprocessing token, followed by an identifier,
 * occurs lexically at the point at which a preprocessing directive could
 * begin, the identifier is not subject to macro replacement."
 */
void preprocessor::execute_directive(const preprocessing_token &hash)
{
    directive_end = hash.location + hash.extent;
    preprocessing_token name = directive_token();
    /* [cpp.null]/1: "A preprocessing directive of the form # new-line has
       no effect." */
    if (is_end(name))
        return;
    /* [ext.gnu] gnu_line_marker: # digit-sequence "s-char-sequence" flags */
    if (name.kind == preprocessing_token_kind::pp_number &&
        behavior.has(extension::gnu_line_marker)) {
        gnu_line_marker(name);
        return;
    }
    directive_kind kind = directive_kind::unknown;
    owned_buffer spelling;
    if (is_identifier(name)) {
        files.spelling(name, spelling.get());
        kind = find_directive(behavior, spelling.data(), spelling.size());
    }
    /* [cpp.replace]/13's undefined behavior: [impl.clang] #include and
       #pragma in a macro's arguments are errors, the others are executed */
    if (collecting_arguments &&
        (kind == directive_kind::include ||
         kind == directive_kind::include_next ||
         kind == directive_kind::import || kind == directive_kind::pragma)) {
        (diagnostics.issue(diagnostic_id::err_embedded_directive, name.location)
         << 1)
            .text(spelling.data(), spelling.size());
        skip_rest_of_directive();
        return;
    }
    switch (kind) {
    case directive_kind::include:
    case directive_kind::include_next:
    case directive_kind::import:
        directive_include(name, kind);
        return;
    case directive_kind::define:
        directive_define(name);
        return;
    case directive_kind::undef:
        directive_undef(name);
        return;
    case directive_kind::line:
        directive_line(name);
        return;
    case directive_kind::error:
    case directive_kind::warning:
        directive_error(name, kind == directive_kind::warning);
        return;
    case directive_kind::pragma:
        directive_pragma(name);
        return;
    case directive_kind::if_:
    case directive_kind::ifdef:
    case directive_kind::ifndef:
        directive_if(name, kind);
        return;
    case directive_kind::elif:
    case directive_kind::elifdef:
    case directive_kind::elifndef:
    case directive_kind::else_:
        directive_elif_else(name, kind);
        return;
    case directive_kind::endif:
        directive_endif(name);
        return;
    case directive_kind::ident: /* [ext.gnu] */
        gnu_ident_directive(name);
        return;
    case directive_kind::embed: /* [ext.clang] */
        clang_embed_directive(name);
        return;
    default:
        /* [cpp.pre]/2: "A conditionally-supported-directive is
           conditionally-supported with implementation-defined semantics":
           [impl.clang] none is supported, as in clang */
        diagnostics.issue(diagnostic_id::err_pp_invalid_directive,
                          name.location)
            << 0;
        skip_rest_of_directive();
        return;
    }
}

/* [lex.phases]/1.4: phase 4's output */
preprocessing_token preprocessor::next()
{
    preprocessing_token t;
    for (;;) {
        read_unexpanded(t);
        if (embed_continues_line && read_from_file) {
            t.at_start_of_line = false;
            embed_continues_line = false;
        }
        if (pending_flags) {
            /* a macro replaced by nothing leaves its line start and white
               space to the next token on its line ([impl.clang]) */
            if (!(read_from_file && t.at_start_of_line)) {
                t.at_start_of_line |= pending_start_of_line;
                t.after_white_space |= pending_white_space;
            }
            pending_flags = false;
        }
        if (t.kind == preprocessing_token_kind::identifier) {
            if (!t.expansion && t.at_start_of_line &&
                behavior.has(language_feature::modules) &&
                module_or_import_directive(t))
                break;
            if (t.not_replaceable)
                break;
            uint32_t id = identifier(t);
            /* [impl.clang] only as written in a source file */
            if (identifiers[id].poisoned && !t.expansion)
                diagnostics.issue(diagnostic_id::err_pp_used_poisoned_id,
                                  t.location);
            int32_t m = identifiers[id].macro;
            if (m >= 0 && replace_macro(t, m))
                continue;
        }
        break;
    }
    return t;
}

} // namespace cplusplus
