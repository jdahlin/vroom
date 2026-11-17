/*
 * pragma_operator.cpp: [ext:microsoft_pragma_operator]: MSVC's
 *
 *     __pragma ( pp-tokens_opt )
 *
 * which, unlike [cpp.pragma.op]'s _Pragma, takes the pragma's tokens as
 * they are, not in a string literal; the tokens up to the matching ) are
 * executed as a #pragma directive's and removed.
 */
#include "../../cplusplus-internal.h"

namespace cplusplus
{

void preprocessor::microsoft_define_dynamic_macros()
{
    define_dynamic("__pragma", dynamic_macro::microsoft_pragma, false);
}

bool preprocessor::microsoft_pragma_operator(const preprocessing_token &name)
{
    preprocessing_token t;
    if (!read_unexpanded(t) ||
        !is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        quoted(diagnostics.issue(diagnostic_id::err_pp_expected_after,
                                 name.location),
               name)
            << "'('";
        return true;
    }
    owned_array<preprocessing_token> tokens;
    unsigned depth = 0;
    for (;;) {
        if (!read_unexpanded(t) ||
            t.kind == preprocessing_token_kind::end_of_file) {
            diagnostics.issue(diagnostic_id::err_unterminated___pragma,
                              name.location);
            return true;
        }
        if (is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS))
            depth++;
        else if (is_operator(t, preprocessing_op_or_punc::RIGHT_PARENTHESIS) &&
                 depth-- == 0)
            break;
        tokens.append(t);
    }
    pragma_end = t.location;
    execute_pragma(tokens.data(), tokens.size(), name.location);
    return true;
}

} // namespace cplusplus
