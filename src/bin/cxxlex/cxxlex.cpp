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

int main(int argc, char **argv)
{
    implementation_defined_behavior behavior =
        implementation_defined_behavior::for_edition(
            standard_edition::cplusplus_17, true);
    diagnostic_options diagnostic_settings;
    bool raw = false, char8_set = false, char8 = false, trigraph_set = false,
         trigraphs = false;
    bool dollar_set = false, dollar = true, ms = false, ms_compat = false;
    const char *path = nullptr;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-dump-raw-tokens"))
            raw = true;
        else if (!strcmp(a, "-dump-tokens"))
            raw = false;
        else if (!strncmp(a, "-std=", 5)) {
            if (!parse_edition(a + 5, &behavior)) {
                fprintf(stderr, "cxxlex: unsupported %s\n", a);
                return 2;
            }
        } else if (!strcmp(a, "-fms-extensions"))
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
        fprintf(stderr, "usage: cxxlex [-dump-raw-tokens|-dump-tokens] [clang "
                        "-cc1 options] FILE\n");
        return 2;
    }
    if (char8_set)
        behavior.char8_t_keyword = char8;
    if (trigraph_set)
        behavior.trigraphs = trigraphs;
    if (dollar_set)
        behavior.dollar_in_identifier = dollar;
    behavior.microsoft_extensions = ms;
    behavior.microsoft_compatibility = ms_compat;

    source_file file;
    if (!file.open(path)) {
        fprintf(stderr, "cxxlex: cannot read %s\n", path);
        return 2;
    }
    diagnostic_consumer diagnostics(diagnostic_settings);
    diagnostics.file = &file;
    lexer_options options;
    options.retain_white_space = options.retain_comments = raw;
    lexer l(file, behavior, diagnostics, options);

    vbuffer out = {};
    vbuffer spelling = {};
    for (;;) {
        preprocessing_token pp = l.next();
        if (pp.kind == preprocessing_token_kind::end_of_file && raw)
            break;
        /* -dump-tokens on preprocessed input: phase 4 is a line directive
           ([cpp.line]/5: "# digit-sequence "s-char-sequence"") or another
           directive to skip */
        while (!raw && pp.at_start_of_line &&
               pp.kind == preprocessing_token_kind::preprocessing_op_or_punc &&
               preprocessing_op_or_punc_primary(pp.op_or_punc) ==
                   preprocessing_op_or_punc::HASH) {
            preprocessing_token number = l.next();
            preprocessing_token name = number;
            if (!number.at_start_of_line &&
                number.kind == preprocessing_token_kind::pp_number) {
                name = l.next();
                spelling.n = 0;
                l.spelling(number, &spelling);
                unsigned long line = strtoul(spelling.p, nullptr, 10);
                if (!name.at_start_of_line &&
                    name.kind == preprocessing_token_kind::string_literal) {
                    spelling.n = 0;
                    l.spelling(name, &spelling);
                    char *presumed = strndup(spelling.p + 1, spelling.n - 2);
                    preprocessing_token rest = name;
                    while (!(rest = l.next()).at_start_of_line &&
                           rest.kind != preprocessing_token_kind::end_of_file)
                        ;
                    /* [cpp.line]/4: the line after the directive's new-line */
                    uint32_t next = name.location + name.extent;
                    while (next < file.size &&
                           file.physical_characters[next] != '\n')
                        next++;
                    file.add_line_directive(next + 1, (uint32_t)line, presumed);
                    pp = rest;
                } else
                    pp = name;
            } else
                pp = number;
            while (!pp.at_start_of_line &&
                   pp.kind != preprocessing_token_kind::end_of_file)
                pp = l.next();
        }
        const char *name;
        token t;
        if (raw)
            name = clang_token_name(l, pp);
        else {
            t = l.convert(pp);
            name = clang_token_name(l, t);
        }
        spelling.n = 0;
        if (pp.kind != preprocessing_token_kind::end_of_file)
            l.spelling(pp, &spelling);
        source_position p = file.position(pp.location);
        vbuffer_format_append(&out, "%s '", name);
        write_escaped(&out, spelling.p ? spelling.p : "", spelling.n);
        vbuffer_format_append(&out, "'\tLoc=<%s:%u:%u>", p.file_name, p.line,
                              p.column);
        if (pp.at_start_of_line)
            vbuffer_string_append(&out, " [StartOfLine]");
        if (pp.after_white_space && !raw)
            vbuffer_string_append(&out, " [LeadingSpace]");
        if (pp.spelling_differs) {
            vbuffer_string_append(&out, " [UnClean='");
            write_escaped(
                &out,
                reinterpret_cast<const char *>(file.physical_characters) +
                    pp.location,
                pp.extent);
            vbuffer_string_append(&out, "']");
        }
        vbuffer_string_append(&out, "\n");
        if (pp.kind == preprocessing_token_kind::end_of_file)
            break;
    }
    fwrite(out.p, 1, out.n, stdout);

    vbuffer messages = {};
    for (size_t i = 0; i < diagnostics.message_count; i++)
        diagnostics.render(diagnostics.messages[i], &messages);
    fwrite(messages.p, 1, messages.n, stderr);
    free(out.p);
    free(spelling.p);
    free(messages.p);
    return diagnostics.error_count ? 1 : 0;
}
