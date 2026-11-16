/*
 * cpp.module.cpp: [cpp.module] Module directive.
 *
 *     pp-module:
 *         'export'_opt 'module' pp-tokens_opt ';' new-line
 *
 * [cpp.pre]/1: a directive-introducing token is also "a module
 * preprocessing token immediately followed on the same logical line by an
 * identifier, :, or ; preprocessing token, or an export preprocessing token
 * immediately followed on the same logical line by one of the two preceding
 * forms".  [cpp.module]/3: "The module and export (if it exists)
 * preprocessing tokens are replaced by the module-keyword and
 * export-keyword preprocessing tokens respectively. [Note 2: This makes the
 * line no longer a directive so it is not removed at the end of phase 4.]"
 * [cpp.module]/2: "Any preprocessing tokens after the module preprocessing
 * token in the module directive are processed just as in normal text."
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/* whether `next` makes a module token before it a directive-introducing
   token ([cpp.pre]/1) */
bool preprocessor::begins_module_directive(const preprocessing_token &next)
{
    return !next.at_start_of_line &&
           (is_identifier(next) ||
            is_operator(next, preprocessing_op_or_punc::COLON) ||
            is_operator(next, preprocessing_op_or_punc::SEMICOLON));
}

/* [cpp.module]/1: "A pp-module shall not appear in a context where module
   or (if it is the first token of the pp-module) export is an identifier
   defined as an object-like macro." */
bool preprocessor::names_object_like_macro(const preprocessing_token &t)
{
    int32_t m = identifiers[identifier(t)].macro;
    return m >= 0 && !macros[m].function_like;
}

} // namespace cplusplus
