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
    /* ... and the check in vcc that refused it (unsupported_construct) */
    const char *file = nullptr;
    uint32_t line = 0;
    const char *function = nullptr;
    /* a declaration kept as tokens: where the parse stopped and what it
       expected there (parser::unparsed_reason), at 0 if not known */
    token_index parse_at = 0;
    const char *expected = nullptr, *parse_subclause = nullptr;
    bool later_step = false;
    /* the analysis's (analysis_complete), not code generation's */
    bool front_end = false;
};

/* whether every declaration of the translation unit is well-formed; false,
   and why (as lower_translation_unit gives it), at the first that is not */
bool analysis_complete(const semantic_analysis &analysis,
                       lowering_failure &why);

/* every function and variable of a translation unit whose declarations are
   all well-formed, added to c (ast_builder_open) with its definition;
   false, and why, at the first construct not lowered.  main_path is the
   main source file's path, which names the function that runs the
   dynamic initializations */
bool lower_translation_unit(const semantic_analysis &analysis,
                            token_stream &tokens, cc *c, int target,
                            lowering_failure &why,
                            const char *main_path = nullptr);

} // namespace cplusplus

#endif
