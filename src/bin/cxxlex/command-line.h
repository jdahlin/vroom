/*
 * command-line.h: what cxxlex and cxxparse share: the clang -cc1 options
 * that change how a translation unit is read, and a token printed as clang
 * -cc1 -dump-tokens prints it (bench/cxx-lex.py, cxx-pp.py and cxx-parse.py
 * compare the tools with clang and with each other).
 *
 * The options: -std=, -x, -D, -U, -I, -iquote, -isystem, -idirafter,
 * --embed-dir=, -undef, -fms-extensions, -fms-compatibility,
 * -fdollars-in-identifiers, -fno-dollars-in-identifiers, -fchar8_t,
 * -fno-char8_t, -trigraphs, -ftrigraphs, -fno-trigraphs,
 * -fno-operator-names, -pedantic, -pedantic-errors, -W..., -w.
 */
#ifndef VCC_CXXLEX_COMMAND_LINE_H
#define VCC_CXXLEX_COMMAND_LINE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frontend/cplusplus/cplusplus.h"

using namespace cplusplus;

static inline void write_escaped(vbuffer *out, const char *s, size_t n)
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

static inline bool parse_edition(const char *s,
                                 implementation_defined_behavior *b)
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
    if (gnu)
        s += 5;
    else
        s += 3;
    for (const auto &n : names)
        if (!strcmp(s, n.name)) {
            *b = implementation_defined_behavior::for_edition(n.edition, gnu);
            return true;
        }
    return false;
}

/* one token as clang -cc1 -dump-raw-tokens / -dump-tokens prints it */
static inline void write_token(vbuffer *out, const char *name,
                               const char *spelling, size_t n,
                               source_position p, const source_files *files,
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

struct command_line {
    const char *tool;
    implementation_defined_behavior behavior =
        implementation_defined_behavior::for_edition(
            standard_edition::cplusplus_17, true);
    diagnostic_options diagnostic_settings;
    preprocessor_options pp_options;
    bool char8_set = false, char8 = false, trigraph_set = false,
         trigraphs = false;
    bool dollar_set = false, dollar = true, ms = false, ms_compat = false;
    bool no_operator_names = false;

    explicit command_line(const char *tool) : tool(tool)
    {
    }

    /* argv[i] if it is one of the shared options (i moves past an operand
       given apart); false if it is the tool's own or the file */
    bool option(int argc, char **argv, int &i)
    {
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
            if (a[matched])
                operand = a + matched;
            else if (i + 1 < argc)
                operand = argv[++i];
            else
                operand = "";
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
                include_search search = include_search::system;
                if (!strcmp(a, "-iquote") || !strncmp(a, "-iquote", 7))
                    search = include_search::quote;
                else if (!strncmp(a, "-I", 2))
                    search = include_search::angled;
                else if (!strncmp(a, "-idirafter", 10))
                    search = include_search::after;
                pp_options.include_directories.append({operand, search});
            }
            return true;
        }
        if (!strncmp(a, "--embed-dir=", 12)) {
            pp_options.embed_directories.append(a + 12);
            return true;
        }
        if (!strcmp(a, "-undef"))
            pp_options.predefined_macros = false;
        else if (!strncmp(a, "-std=", 5)) {
            if (!parse_edition(a + 5, &behavior)) {
                fprintf(stderr, "%s: unsupported %s\n", tool, a);
                exit(2);
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
        else
            return false;
        return true;
    }

    /* after the last option: the choices the options made, in behavior */
    void apply()
    {
        if (char8_set)
            behavior.char8_t_keyword = char8;
        if (trigraph_set)
            behavior.trigraphs = trigraphs;
        if (dollar_set)
            behavior.dollar_in_identifier = dollar;
        behavior.microsoft_extensions = ms;
        behavior.operator_names = !no_operator_names;
        behavior.microsoft_compatibility = ms_compat;
    }
};

#endif
