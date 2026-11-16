/*
 * cpp.line.cpp: [cpp.line] Line control.  GNU's line markers are
 * ext/gnu/line_marker.cpp.
 *
 * [cpp.line]/2: "The line number of the current source line is one greater
 * than the number of new-line characters read or introduced in translation
 * phase 1 while processing the source file to the current token."
 */
#include <stdio.h>
#include <stdlib.h>

#include "../cplusplus-internal.h"

namespace cplusplus
{

/* [cpp.line]/3: "If the digit sequence specifies zero or a number greater
   than 2147483647, the behavior is undefined"; clang allows both, with
   warnings.  False if the token is no digit-sequence. */
bool preprocessor::line_number(const preprocessing_token &t, bool gnu_marker,
                               uint32_t *value)
{
    if (t.kind != preprocessing_token_kind::pp_number) {
        diagnostics.issue(
            gnu_marker ? diagnostic_id::err_pp_linemarker_requires_integer
                       : diagnostic_id::err_pp_line_requires_integer,
            t.location);
        return false;
    }
    owned_buffer s;
    files.spelling(t, s.get());
    uint32_t v = 0;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s.data()[i];
        /* a digit separator ([lex.icon]) is not part of a digit-sequence */
        if (c < '0' || c > '9') {
            diagnostics.issue(diagnostic_id::err_pp_line_digit_sequence,
                              t.location)
                << (int)gnu_marker;
            return false;
        }
        uint32_t next = v * 10 + (uint32_t)(c - '0');
        if (next < v) {
            diagnostics.issue(
                gnu_marker ? diagnostic_id::err_pp_linemarker_requires_integer
                           : diagnostic_id::err_pp_line_requires_integer,
                t.location);
            return false;
        }
        v = next;
    }
    if (s.data()[0] == '0' && v)
        diagnostics.issue(diagnostic_id::warn_pp_line_decimal, t.location)
            << (int)gnu_marker;
    *value = v;
    return true;
}

/* the contents of a character string literal ([cpp.line]/4), its escape
   sequences replaced; false if the token is none */
bool preprocessor::string_contents(const preprocessing_token &t,
                                   owned_buffer &out)
{
    if (t.kind != preprocessing_token_kind::string_literal ||
        t.prefix != encoding_prefix::none)
        return false;
    token converted = files.convert(t);
    const uint32_t *units = files.phase7().string_code_units();
    out.clear();
    for (uint32_t i = 0; i < converted.string_length; i++) {
        char c = (char)units[converted.string_offset + i];
        vbuffer_append(out.get(), &c, 1);
    }
    return true;
}

/* the presumed line and name start at the line after the directive */
void preprocessor::set_presumed_line(uint32_t line, const char *name,
                                     size_t length)
{
    source_files::entry &e = *inclusions.back().file;
    uint32_t next = directive_end - e.base;
    while (next < e.file.size && e.file.physical_characters[next] != '\n' &&
           e.file.physical_characters[next] != '\r')
        next++;
    if (next < e.file.size && e.file.physical_characters[next] == '\r' &&
        e.file.physical_characters[next + 1] == '\n')
        next++;
    if (!name) {
        source_position p = e.file.position(next);
        name = p.file_name;
        length = strlen(name);
    }
    owned_string copy(name, length);
    e.file.add_line_directive(next + 1, line, copy.c_str(), length);
}

/*
 * [cpp.line]/3: "# line digit-sequence new-line causes the implementation
 * to behave as if the following sequence of source lines begins with a
 * source line that has a line number as specified by the digit sequence";
 * /4 with a string-literal "changes the presumed name of the source file";
 * /5 "The preprocessing tokens after line on the directive are processed
 * just as in normal text".
 */
void preprocessor::directive_line(const preprocessing_token &name)
{
    (void)name;
    owned_array<preprocessing_token> line, operands;
    for (preprocessing_token t = directive_token(); !is_end(t);
         t = directive_token())
        line.append(t);
    size_t depth = contexts.size();
    push_tokens(line.data(), line.size(), -1, true);
    for (preprocessing_token t; read_expanded(t);)
        operands.append(t);
    while (contexts.size() > depth)
        pop_context();
    if (operands.empty()) {
        diagnostics.issue(diagnostic_id::err_pp_line_requires_integer,
                          directive_end);
        return;
    }
    uint32_t number;
    if (!line_number(operands[0], false, &number))
        return;
    /* [cpp.line]/3: "If the digit sequence specifies zero or a number
       greater than 2147483647, the behavior is undefined": [impl.clang]
       both are accepted, with warnings, and C++98's limit is 32767 */
    if (number == 0)
        diagnostics.issue(diagnostic_id::ext_pp_line_zero,
                          operands[0].location);
    uint32_t limit =
        behavior.has(language_feature::long_long) ? 2147483648u : 32768u;
    if (number >= limit)
        diagnostics.issue(diagnostic_id::ext_pp_line_too_big,
                          operands[0].location)
            << (int64_t)limit;
    else if (behavior.has(language_feature::long_long) && number >= 32768)
        diagnostics.issue(diagnostic_id::warn_cxx98_compat_pp_line_too_big,
                          operands[0].location);
    owned_buffer presumed;
    if (operands.size() > 1 && !string_contents(operands[1], presumed)) {
        /* [cpp.line]/1: "The string-literal of a #line directive, if
           present, shall be a character string literal." */
        diagnostics.issue(diagnostic_id::err_pp_line_invalid_filename,
                          operands[1].location);
        return;
    }
    if (operands.size() > 2)
        diagnostics.issue(diagnostic_id::ext_pp_extra_tokens_at_eol,
                          operands[2].location)
            << "line";
    if (operands.size() > 1)
        set_presumed_line(number, presumed.data(), presumed.size());
    else
        set_presumed_line(number, nullptr, 0);
}

} // namespace cplusplus
