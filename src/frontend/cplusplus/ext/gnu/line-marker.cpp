/*
 * line-marker.cpp: [ext:gnu_line_marker] GNU's line markers, the line
 * control GCC's and clang's -E write (GNU cpp manual 9, Preprocessor
 * Output):
 *
 *     control-line:
 *         # digit-sequence " s-char-sequence_opt " pp-tokens_opt new-line
 *
 * a # followed by a pp-number where a directive name would be, which
 * [cpp.pre]/2 makes a conditionally-supported-directive.  The pp-tokens are
 * the flags: 1 a file is entered, 2 one is left, 3 the following text is a
 * system header, 4 it is in an extern "C" block.
 */
#include "../../cplusplus-internal.h"

namespace cplusplus
{

void preprocessor::gnu_line_marker(const preprocessing_token &digits)
{
    owned_array<preprocessing_token> operands;
    operands.append(digits);
    for (preprocessing_token t = directive_token(); !is_end(t);
         t = directive_token())
        operands.append(t);
    uint32_t number;
    if (!line_number(digits, true, &number))
        return;
    owned_buffer presumed;
    if (operands.size() > 1 && !string_contents(operands[1], presumed)) {
        diagnostics.issue(diagnostic_id::err_pp_linemarker_invalid_filename,
                          operands[1].location);
        return;
    }
    bool system_header = false;
    for (size_t i = 2; i < operands.size(); i++) {
        owned_buffer flag;
        files.spelling(operands[i], flag.get());
        if (operands[i].kind != preprocessing_token_kind::pp_number ||
            flag.size() != 1 || flag.data()[0] < '1' || flag.data()[0] > '4') {
            diagnostics.issue(diagnostic_id::err_pp_linemarker_invalid_flag,
                              operands[i].location);
            return;
        }
        system_header |= flag.data()[0] == '3';
    }
    files.mark_system_header(directive_end, system_header);
    /* clang warns of a line marker written in a source file (-Wgnu-line-marker,
       with -pedantic), not of those in its predefined macros' */
    if (inclusions.back().file->index != predefines_file)
        diagnostics.issue(
            diagnostic_id::ext_pp_gnu_line_directive,
            location_of(operands.data(), operands.size(), 1, directive_end));
    if (operands.size() > 1)
        set_presumed_line(number, presumed.data(), presumed.size());
    else
        set_presumed_line(number, nullptr, 0);
}

} // namespace cplusplus
