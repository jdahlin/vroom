/*
 * deleted-definition.cpp: [dcl.fct.def.delete] Deleted definitions.
 *
 * [dcl.fct.def.delete]/1: "A function definition whose function-body is of
 * the form = delete ; is called a deleted definition.  A function with a
 * deleted definition is also called a deleted function."  /2: "A program
 * that refers to a deleted function implicitly or explicitly, other than to
 * declare it, is ill-formed.  [Note 1: ... If a function is overloaded, it
 * is referenced only if the function is selected by overload resolution.
 * ...]"  /4: "A deleted function is implicitly an inline function
 * ([dcl.inline]). ... A deleted definition of a function shall be the
 * first declaration of the function".
 *
 * The parser's function-body is = delete ; (and P2573's = delete (
 * string-literal ) ;, whose message analysis does not keep).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

bool semantic_analysis::deleted_body(SyntaxElement body) const
{
    return explicit_body(body, standard_keyword::delete_);
}

/* the function-body = delete ... ; or = default ; (keyword k) */
bool semantic_analysis::explicit_body(SyntaxElement body,
                                      standard_keyword k) const
{
    if (body.is_token() || !body || !body.is(syntax_kind::function_body) ||
        body.size() < 3)
        return false;
    return body[0].is(preprocessing_op_or_punc::EQUAL) && body[1].is_token() &&
           body[1].keyword() == k;
}

analysis_result semantic_analysis::deleted_definition(SyntaxRef n,
                                                      entity_index f)
{
    entity &x = entity_table[f];
    if (x.first_declaration != x.last_declaration)
        /* /4 */
        return ill_formed(n,
                          "a deleted definition of a function shall be the "
                          "first declaration of the function",
                          "dcl.fct.def.delete");
    x.deleted = true;
    x.inline_ = true;
    return analysis_result::well_formed;
}

/* clang: "'f' is unavailable" for a use of an entity declared with the
   unavailable attribute, except where the use is in an unavailable function
   (the diagnostic is delayed there, as a deprecated one is) */
analysis_result semantic_analysis::unavailable_use(token_index at,
                                                   entity_index e)
{
    if (!(entity_table[e].gnu_flags & gnu_unavailable))
        return analysis_result::well_formed;
    /* clang delays the diagnostic in an unavailable function, and in a member
       of an unavailable class; a default argument is analysed before the
       function's attributes are all known */
    if (default_argument_function)
        return not_supported(current_declaration, "ext.gnu");
    if (function_state.active && function_state.function)
        for (entity_index c = function_state.function; c;
             c = entity_table[c].enclosing)
            if (entity_table[c].gnu_flags & gnu_unavailable)
                return analysis_result::well_formed;
    return ill_formed_at_token(at, "the entity is declared unavailable",
                               "ext.gnu");
}

analysis_result semantic_analysis::deleted_use(token_index at, entity_index f)
{
    VTRY(u, unavailable_use(at, f));
    if (!entity_table[f].deleted)
        return analysis_result::well_formed;
    /* /2 */
    return ill_formed_at_token(at,
                               "a program that refers to a deleted "
                               "function, other than to declare it, is "
                               "ill-formed",
                               "dcl.fct.def.delete");
}

} // namespace cplusplus
