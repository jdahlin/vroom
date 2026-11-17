/*
 * cpp.concat.cpp: [cpp.concat] The ## operator.
 *
 * [cpp.concat]/3: "Placemarker preprocessing tokens are handled specially:
 * concatenation of two placemarkers results in a single placemarker
 * preprocessing token, and concatenation of a placemarker with a
 * non-placemarker preprocessing token results in the non-placemarker
 * preprocessing token.  If the result is not a valid preprocessing token,
 * the behavior is undefined.  The resulting token is available for further
 * macro replacement."
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/* left becomes the concatenation of left and right; false if they do not
   form one preprocessing token: [impl.clang] an error, both kept
   ([ext:microsoft_invalid_paste]: a warning) */
bool preprocessor::concatenate(preprocessing_token &left,
                               const preprocessing_token &right,
                               source_location at)
{
    if (right.kind == preprocessing_token_kind::placemarker)
        return true;
    if (left.kind == preprocessing_token_kind::placemarker) {
        bool white_space = left.after_white_space;
        left = right;
        left.after_white_space = white_space;
        return true;
    }
    owned_buffer text;
    files.spelling(left, text.get());
    files.spelling(right, text.get());
    preprocessing_token result;
    if (!spelled(text.data(), text.size(), &result)) {
        diagnostic_id id = diagnostic_id::err_pp_bad_paste;
        if (behavior.has(extension::microsoft_invalid_paste))
            id = diagnostic_id::ext_pp_bad_paste_ms;
        diagnostics.issue(id, at).text(text.data(), text.size());
        return false;
    }
    result.at_start_of_line = left.at_start_of_line;
    result.after_white_space = left.after_white_space;
    left = result;
    return true;
}

} // namespace cplusplus
