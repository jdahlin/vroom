/*
 * function-specifier.cpp: [dcl.fct.spec] Function specifiers.
 *
 *     function-specifier:
 *         virtual
 *         explicit-specifier
 *
 *     explicit-specifier:
 *         explicit ( constant-expression )
 *         explicit
 *
 * virtual and explicit alone are their tokens in the decl-specifier-seq
 * (decl-specifier.cpp).  [dcl.fct.spec]/3: "In an explicit-specifier, the
 * constant-expression, if supplied, shall be a contextually converted
 * constant expression of type bool": a semantic rule.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/* explicit ( constant-expression ): P0892R2 (C++20); before it explicit
   followed by ( is clang's extension with a warning
   ([ext:clang_later_edition_features]; libc++ uses it in every mode) */
node_index parser::explicit_specifier()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* explicit */
    add_token(); /* ( */
    constant_expression();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to close the explicit-specifier", "dcl.fct.spec");
    return finish_node(syntax_kind::explicit_specifier, mark, first);
}

} // namespace cplusplus
