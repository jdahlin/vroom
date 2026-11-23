/*
 * ident.cpp: [ext:gnu_ident_directive] (GNU cpp manual 11, Other
 * Directives):
 *
 *     control-line:
 *         # ident pp-tokens new-line
 *         # sccs pp-tokens new-line
 *
 * whose pp-tokens are a string-literal, which GCC puts in the object
 * file's comment section; clang accepts and drops them.
 */
#include "../../cplusplus-internal.h"

namespace cplusplus
{

void preprocessor::gnu_ident_directive(const preprocessing_token &name)
{
    diagnostics.issue(diagnostic_id::ext_pp_ident_directive, name.location);
    preprocessing_token s = directive_token();
    if (!is_string_literal(s))
        diagnostics.issue(diagnostic_id::err_pp_malformed_ident, s.location);
    skip_rest_of_directive();
}

} // namespace cplusplus
