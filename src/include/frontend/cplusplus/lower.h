#ifndef VCC_FRONTEND_CPLUSPLUS_LOWER_H
#define VCC_FRONTEND_CPLUSPLUS_LOWER_H
/*
 * lower.h: the semantic tree of a translation unit as the AST C's code
 * generator reads (frontend/c/ast-builder.h; doc/PLAN-cxx-sema.md S1d).
 */
#include "frontend/c/ast-builder.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* why a translation unit was not lowered: the first construct lowering has
   no rule for, at a token */
struct lowering_failure {
    const char *what = nullptr;
    token_index at = 0;
    /* a declaration the analysis does not support: the subclause whose
       rules are missing (unsupported_construct), at its first token */
    const char *subclause = nullptr;
};

/* every function and variable of a translation unit whose declarations are
   all well-formed, added to c (ast_builder_open) with its definition;
   false, and why, at the first construct not lowered */
bool lower_translation_unit(const semantic_analysis &analysis,
                            token_stream &tokens, cc *c, int target,
                            lowering_failure &why);

} // namespace cplusplus

#endif
