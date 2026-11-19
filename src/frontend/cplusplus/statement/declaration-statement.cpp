/*
 * declaration-statement.cpp: [stmt.dcl] Declaration statement.
 *
 *     declaration-statement:
 *         block-declaration
 *
 * [stmt.dcl]/1: "A declaration statement introduces one or more new names
 * into a block".  The block-declaration passes through (its alternatives
 * are [dcl.pre]'s, block_or_function_declaration and the others).
 *
 *     block-declaration:
 *         simple-declaration
 *         asm-declaration
 *         namespace-alias-definition
 *         using-declaration
 *         using-enum-declaration
 *         using-directive
 *         static_assert-declaration
 *         alias-declaration
 *         opaque-enum-declaration
 */
#include "frontend/cplusplus/sema.h"
#include "ambiguity-resolution.h"

namespace cplusplus
{

node_index parser::block_declaration()
{
    if (at(extension_keyword::extension)) /* [ext.gnu] */
        return gnu_extension_prefixed(&parser::block_declaration);
    flag_set block(block_scope, true);
    unsigned after = attribute_specifier_seq_end(0);
    if (after) {
        if (at(standard_keyword::using_, after) &&
            at(standard_keyword::namespace_, after + 1))
            return using_directive();
        if (at(standard_keyword::asm_, after))
            return asm_declaration();
        return block_or_function_declaration();
    }
    if (at(standard_keyword::namespace_)) {
        if (at_identifier(1) && at(preprocessing_op_or_punc::EQUAL, 2))
            return namespace_alias_definition();
        /* [namespace.def]: a namespace-definition is a declaration, not a
           block-declaration */
        return expected("a block-declaration", "stmt.dcl");
    }
    if (at(standard_keyword::using_) && at(standard_keyword::namespace_, 1))
        return using_directive();
    node_index n = 0;
    if (shared_declaration_alternative(&n))
        return n;
    if (at(standard_keyword::asm_))
        return asm_declaration();
    if (at(standard_keyword::template_) || at(standard_keyword::export_))
        /* a template-declaration is no block-declaration ([temp.pre]/2:
           "A template-declaration can appear only as a namespace scope or
           class scope declaration"): P7 says so */
        return beyond_this_step();
    return block_or_function_declaration();
}

/* ---- semantic analysis ---------------------------------------------- */

/* whether the variable declared at variable is in scope at at: declared
   before it, in its scope or one enclosing it */
bool semantic_analysis::in_scope(jump_point variable, jump_point at) const
{
    if (variable.at >= at.at)
        return false;
    for (entity_index s = at.scope;; s = entity_table[s].enclosing) {
        if (s == variable.scope)
            return true;
        if (!s)
            return false;
    }
}

/*
 * [stmt.dcl]/3: "It is possible to transfer into a block, but not in a way
 * that bypasses declarations with initialization (including ones in
 * conditions and init-statements).  A program that jumps from a point where
 * a variable with automatic storage duration is not in scope to a point
 * where it is in scope is ill-formed unless the variable has vacuous
 * initialization".  The variables analysis has types for have vacuous
 * initialization exactly when they have no initializer.
 */
bool semantic_analysis::jump_bypasses(jump_point from, jump_point to) const
{
    for (const jump_point &v : function_state.initialized)
        if (in_scope(v, to) && !in_scope(v, from))
            return true;
    return false;
}

} // namespace cplusplus
