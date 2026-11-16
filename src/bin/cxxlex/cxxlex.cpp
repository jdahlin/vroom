/*
 * cxxlex: the C++ front end's lexer (src/frontend/cplusplus/lex) on one
 * source file, printing what clang -cc1 prints for the same command line, so
 * that bench/cxx-lex.py can compare the two:
 *
 *   cxxlex -dump-raw-tokens [OPTIONS] FILE   phase 3: preprocessing tokens,
 *                                            white space and comments
 *   cxxlex -dump-tokens [OPTIONS] FILE       phase 7: tokens (FILE should be
 *                                            preprocessed: line markers are
 *                                            followed, other directives skipped)
 *   cxxlex -literal-claims [OPTIONS] FILE    the same tokens' literals as a C++
 *                                            file of static_asserts: each
 *                                            literal's type and value as
 *                                            computed here, for clang to check
 *
 * OPTIONS are the clang -cc1 options that change lexing: -std=, -x,
 * -fms-extensions, -fms-compatibility, -fdollars-in-identifiers,
 * -fno-dollars-in-identifiers, -fchar8_t, -fno-char8_t, -trigraphs,
 * -ftrigraphs, -fno-trigraphs, -pedantic, -pedantic-errors, -W..., -w.  Others
 * are ignored.  Diagnostics go to stderr as clang prints them; the exit status
 * is 1 if there was an error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frontend/cplusplus/cplusplus.h"

using namespace cplusplus;

static void write_escaped(vbuffer *out, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\\' || c == '"')
            vbuffer_format_append(out, "\\%c", c);
        else if (c == '\n')
            vbuffer_string_append(out, "\\n");
        else if (c == '\t')
            vbuffer_string_append(out, "\\t");
        else if (c < 0x20 || c >= 0x7f)
            vbuffer_format_append(out, "\\%03o", c);
        else
            vbuffer_append(out, &c, 1);
    }
}

/*
 * A literal's type and value as static_asserts clang can check:
 *   _Static_assert(__is_same(__decltype(LIT), T), "");
 *   _Static_assert(LIT == (T)V, "");       integer and character literals
 *   _Static_assert(LIT[k] == (T)V_k, "");  each code unit of a string literal
 * with #line so that a failed claim names the literal's place (clang's
 * spellings, which every edition has).
 */
static void write_claim(vbuffer *out, const lexer &l, const token &t,
                        const char *spelling, size_t n, source_position p)
{
    if (t.kind != token_kind::literal || !t.value_known ||
        t.literal == literal_kind::user_defined_literal ||
        t.type == fundamental_type::bit_precise_int ||
        t.type == fundamental_type::unsigned_bit_precise_int)
        return;
    const char *type = fundamental_type_name(t.type);
    vbuffer_format_append(out, "#line %u \"%s\"\n", p.line, p.file_name);
    if (t.literal == literal_kind::floating_point_literal) {
        /* the bits of the value: __builtin_bit_cast to an unsigned integer
           of the format's width */
        const char *bits =
            t.type == fundamental_type::float16   ? "unsigned short"
            : t.type == fundamental_type::float_  ? "unsigned int"
            : t.type == fundamental_type::double_ ? "unsigned long long"
                                                  : "unsigned __int128";
        vbuffer_format_append(
            out,
            "_Static_assert(__is_same(__decltype(%.*s), %s), \"\"); "
            "_Static_assert(__builtin_bit_cast(%s, %.*s) == "
            "(((unsigned __int128)0x%llxull << 64) | 0x%llxull), \"\");\n",
            (int)n, spelling, type, bits, (int)n, spelling,
            (unsigned long long)t.floating_bits_high,
            (unsigned long long)t.floating_bits_low);
        return;
    }
    if (t.literal == literal_kind::string_literal) {
        vbuffer_format_append(out,
                              "_Static_assert(__is_same(__decltype(%.*s), "
                              "const %s(&)[%u]), \"\");",
                              (int)n, spelling, type, t.string_length + 1);
        for (uint32_t k = 0; k < t.string_length && k < 64; k++)
            vbuffer_format_append(
                out, " _Static_assert(%.*s[%u] == (%s)0x%xu, \"\");", (int)n,
                spelling, k, type, l.string_code_units()[t.string_offset + k]);
        vbuffer_string_append(out, "\n");
        return;
    }
    vbuffer_format_append(
        out,
        "_Static_assert(__is_same(__decltype(%.*s), %s), \"\"); "
        "_Static_assert(%.*s == (%s)0x%llxull, \"\");\n",
        (int)n, spelling, type, (int)n, spelling, type,
        (unsigned long long)t.value);
}

static bool is_string(const preprocessing_token &pp)
{
    return pp.kind == preprocessing_token_kind::string_literal ||
           pp.kind == preprocessing_token_kind::user_defined_string_literal;
}

static bool parse_edition(const char *s, implementation_defined_behavior *b)
{
    static const struct {
        const char *name;
        standard_edition edition;
    } names[] = {{"98", standard_edition::cplusplus_98},
                 {"03", standard_edition::cplusplus_98},
                 {"0x", standard_edition::cplusplus_11},
                 {"11", standard_edition::cplusplus_11},
                 {"1y", standard_edition::cplusplus_14},
                 {"14", standard_edition::cplusplus_14},
                 {"1z", standard_edition::cplusplus_17},
                 {"17", standard_edition::cplusplus_17},
                 {"2a", standard_edition::cplusplus_20},
                 {"20", standard_edition::cplusplus_20},
                 {"2b", standard_edition::cplusplus_23},
                 {"23", standard_edition::cplusplus_23},
                 {"2c", standard_edition::cplusplus_26},
                 {"26", standard_edition::cplusplus_26}};
    bool gnu = !strncmp(s, "gnu++", 5);
    if (!gnu && strncmp(s, "c++", 3))
        return false;
    s += gnu ? 5 : 3;
    for (const auto &n : names)
        if (!strcmp(s, n.name)) {
            *b = implementation_defined_behavior::for_edition(n.edition, gnu);
            return true;
        }
    return false;
}

/* one token as clang -cc1 -dump-raw-tokens / -dump-tokens prints it */
static void write_token(vbuffer *out, const char *name, const char *spelling,
                        size_t n, source_position p, const source_files *files,
                        const preprocessing_token &pp, bool raw,
                        const unsigned char *physical)
{
    vbuffer_format_append(out, "%s '", name);
    write_escaped(out, spelling, n);
    vbuffer_format_append(out, "'\tLoc=<%s:%u:%u", p.file_name, p.line,
                          p.column);
    if (files && pp.expansion) {
        source_position s = files->position(pp.location);
        vbuffer_format_append(out, " <Spelling=%s:%u:%u>", s.file_name, s.line,
                              s.column);
    }
    vbuffer_string_append(out, ">");
    if (pp.at_start_of_line)
        vbuffer_string_append(out, " [StartOfLine]");
    if (pp.after_white_space && !raw)
        vbuffer_string_append(out, " [LeadingSpace]");
    if (pp.spelling_differs && physical) {
        vbuffer_string_append(out, " [UnClean='");
        write_escaped(out, reinterpret_cast<const char *>(physical), pp.extent);
        vbuffer_string_append(out, "']");
    }
    vbuffer_string_append(out, "\n");
}

int main(int argc, char **argv)
{
    implementation_defined_behavior behavior =
        implementation_defined_behavior::for_edition(
            standard_edition::cplusplus_17, true);
    diagnostic_options diagnostic_settings;
    preprocessor_options pp_options;
    bool raw = false, claims = false, char8_set = false, char8 = false,
         trigraph_set = false, trigraphs = false, preprocess = false,
         definitions = false;
    bool dollar_set = false, dollar = true, ms = false, ms_compat = false;
    /* clang -cc1 -dump-tokens does not convert literals: phase 7's
       diagnostics are not issued (bench/cxx-pp.py) */
    bool phase4_only = false, no_operator_names = false;
    const char *path = nullptr;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        /* -D, -U, -I and their kin take their operand joined or apart */
        const char *operand = nullptr;
        static constexpr const char *with_operand[] = {
            "-D",
            "-U",
            "-I",
            "-iquote",
            "-isystem",
            "-idirafter",
            "-internal-isystem",
            "-internal-externc-isystem"};
        size_t matched = 0;
        for (const char *w : with_operand)
            if (!strncmp(a, w, strlen(w)) && strlen(w) > matched &&
                (strlen(w) <= 2 || !a[strlen(w)]))
                matched = strlen(w);
        if (matched) {
            operand =
                a[matched] ? a + matched : (i + 1 < argc ? argv[++i] : "");
            char kind = a[1];
            if (kind == 'D' || kind == 'U') {
                owned_buffer line;
                if (kind == 'D') {
                    const char *eq = strchr(operand, '=');
                    if (eq)
                        vbuffer_format_append(line.get(), "#define %.*s %s\n",
                                              (int)(eq - operand), operand,
                                              eq + 1);
                    else
                        vbuffer_format_append(line.get(), "#define %s 1\n",
                                              operand);
                } else
                    vbuffer_format_append(line.get(), "#undef %s\n", operand);
                memcpy(pp_options.command_line.extend(line.size()), line.data(),
                       line.size());
            } else {
                include_search search =
                    !strcmp(a, "-iquote") || !strncmp(a, "-iquote", 7)
                        ? include_search::quote
                    : !strncmp(a, "-I", 2)          ? include_search::angled
                    : !strncmp(a, "-idirafter", 10) ? include_search::after
                                                    : include_search::system;
                pp_options.include_directories.append({operand, search});
            }
            continue;
        }
        if (!strncmp(a, "--embed-dir=", 12)) {
            pp_options.embed_directories.append(a + 12);
            continue;
        }
        if (!strcmp(a, "-dump-raw-tokens"))
            raw = true;
        else if (!strcmp(a, "-dump-tokens"))
            raw = false;
        else if (!strcmp(a, "-literal-claims"))
            raw = false, claims = true;
        else if (!strcmp(a, "-phase4-diagnostics-only"))
            phase4_only = true;
        else if (!strcmp(a, "-E"))
            preprocess = true;
        else if (!strcmp(a, "-dM"))
            definitions = true;
        else if (!strcmp(a, "-undef"))
            pp_options.predefined_macros = false;
        else if (!strncmp(a, "-std=", 5)) {
            if (!parse_edition(a + 5, &behavior)) {
                fprintf(stderr, "cxxlex: unsupported %s\n", a);
                return 2;
            }
        } else if (!strcmp(a, "-fno-operator-names"))
            no_operator_names = true;
        else if (!strcmp(a, "-fms-extensions"))
            ms = true;
        else if (!strcmp(a, "-fms-compatibility"))
            ms = ms_compat = true;
        else if (!strcmp(a, "-fchar8_t") || !strcmp(a, "-fno-char8_t"))
            char8_set = true, char8 = a[2] == 'c';
        else if (!strcmp(a, "-trigraphs") || !strcmp(a, "-ftrigraphs") ||
                 !strcmp(a, "-fno-trigraphs"))
            trigraph_set = true, trigraphs = strcmp(a, "-fno-trigraphs") != 0;
        else if (!strcmp(a, "-fdollars-in-identifiers") ||
                 !strcmp(a, "-fno-dollars-in-identifiers"))
            dollar_set = true, dollar = a[2] == 'd';
        else if (diagnostic_settings.parse(a))
            ;
        else if (!strcmp(a, "-x") || !strcmp(a, "-triple") || !strcmp(a, "-o"))
            i++;
        else if (a[0] != '-' || !a[1])
            path = a;
    }
    if (!path) {
        fprintf(stderr, "usage: cxxlex [-dump-raw-tokens|-dump-tokens|-E|-E "
                        "-dM] [clang -cc1 options] FILE\n");
        return 2;
    }
    if (char8_set)
        behavior.char8_t_keyword = char8;
    if (trigraph_set)
        behavior.trigraphs = trigraphs;
    if (dollar_set)
        behavior.dollar_in_identifier = dollar;
    behavior.microsoft_extensions = ms;
    behavior.operator_names = !no_operator_names;
    behavior.microsoft_compatibility = ms_compat;

    diagnostic_consumer diagnostics(diagnostic_settings);
    owned_buffer out, spelling;
    auto finish = [&]() {
        fwrite(out.data(), 1, out.size(), stdout);
        owned_buffer messages;
        for (const diagnostic_message &m : diagnostics.messages)
            diagnostics.render(m, messages.get());
        fwrite(messages.data(), 1, messages.size(), stderr);
        return diagnostics.error_count ? 1 : 0;
    };

    if (raw) {
        /* phase 3 alone, on the file as it is */
        source_file file;
        if (!file.open(path)) {
            fprintf(stderr, "cxxlex: cannot read %s\n", path);
            return 2;
        }
        diagnostics.file = &file;
        lexer_options options;
        options.retain_white_space = options.retain_comments = true;
        lexer l(file, behavior, diagnostics, options);
        for (preprocessing_token pp = l.next();
             pp.kind != preprocessing_token_kind::end_of_file; pp = l.next()) {
            spelling.clear();
            l.spelling(pp, spelling.get());
            write_token(out.get(), clang_token_name(l, pp), spelling.data(),
                        spelling.size(), file.position(pp.location), nullptr,
                        pp, true, file.physical_characters + pp.location);
        }
        return finish();
    }

    /* phases 1 to 4 */
    source_files files(behavior, diagnostics);
    diagnostics.files = &files;
    size_t path_length = strlen(path);
    files.file_options.preprocessed_input =
        (path_length > 3 && !strcmp(path + path_length - 3, ".ii")) ||
        (path_length > 2 && !strcmp(path + path_length - 2, ".i"));
    preprocessor pp(files, pp_options);
    if (!pp.start(path)) {
        fprintf(stderr, "cxxlex: cannot read %s\n", path);
        return 2;
    }
    owned_array<preprocessing_token> tokens;
    for (;;) {
        preprocessing_token t = pp.next();
        tokens.append(t);
        if (t.kind == preprocessing_token_kind::end_of_file)
            break;
    }
    if (definitions) {
        pp.write_definitions(out.get());
        return finish();
    }
    if (preprocess) {
        /* the preprocessing tokens, a new line where a line started, a
           space where white space was (clang -E -P without its spacing
           heuristics) */
        bool first = true;
        for (const preprocessing_token &t : tokens) {
            if (t.kind == preprocessing_token_kind::end_of_file)
                break;
            if (!first && t.at_start_of_line)
                vbuffer_append(out.get(), "\n", 1);
            else if (!first && t.after_white_space)
                vbuffer_append(out.get(), " ", 1);
            first = false;
            files.spelling(t, out.get());
        }
        if (!first)
            vbuffer_append(out.get(), "\n", 1);
        return finish();
    }

    /* phases 5 to 7 */
    const lexer &l = files.phase7();
    if (phase4_only)
        diagnostics.suppressed++;
    auto embedded = [&](const preprocessing_token &t) {
        const source_files::entry *e = files.find(t.location);
        return e && e->scratch && !strcmp(e->name.c_str(), "<embed>");
    };
    for (size_t i = 0; i < tokens.size(); i++) {
        const preprocessing_token &p = tokens[i];
        /* #embed's bytes ([ext.clang]): one line, as clang's annot_embed */
        if (!claims && embedded(p)) {
            while (i + 1 < tokens.size() && embedded(tokens[i + 1]))
                i++;
            vbuffer_string_append(out.get(), "annot_embed ''\tLoc=<embed>\n");
            continue;
        }
        /* [lex.phases]/1.6: adjacent string literals become one token; each
           piece is still printed as clang prints it */
        size_t pieces = 1;
        token t;
        if (is_string(p)) {
            while (i + pieces < tokens.size() && is_string(tokens[i + pieces]))
                pieces++;
            t = files.convert_string_literals(&tokens[i], pieces);
        } else
            t = files.convert(p);
        if (claims) {
            if (p.kind != preprocessing_token_kind::end_of_file) {
                /* the claim is about the concatenation, spelled as its pieces */
                spelling.clear();
                for (size_t k = 0; k < pieces; k++) {
                    if (k)
                        vbuffer_append(spelling.get(), " ", 1);
                    files.spelling(tokens[i + k], spelling.get());
                }
                write_claim(out.get(), l, t, spelling.data(), spelling.size(),
                            files.position(p.location));
            }
            i += pieces - 1;
            continue;
        }
        for (size_t k = 0; k < pieces; k++) {
            const preprocessing_token &piece = tokens[i + k];
            const char *name = pieces > 1 ? clang_token_name(l, piece)
                                          : clang_token_name(l, t);
            spelling.clear();
            if (piece.kind != preprocessing_token_kind::end_of_file)
                files.spelling(piece, spelling.get());
            const source_files::entry *e = files.find(piece.location);
            write_token(out.get(), name, spelling.data(), spelling.size(),
                        files.position(piece.expansion ? piece.expansion
                                                       : piece.location),
                        &files, piece, false,
                        e && !piece.expansion ? e->file.physical_characters +
                                                    piece.location - e->base
                                              : nullptr);
        }
        i += pieces - 1;
    }
    return finish();
}
