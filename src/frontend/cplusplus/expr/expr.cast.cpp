/*
 * expr.cast.cpp: [expr.cast] Explicit type conversion (cast notation).
 *
 *     cast-expression:
 *         unary-expression
 *         ( type-id ) cast-expression
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * [expr.cast]/1: "The result of the expression (T) cast-expression is of
 * type T."  Whether a '(' begins the second alternative or a
 * unary-expression (a parenthesized expression, [expr.prim.paren]) is
 * [dcl.ambig.res]/2's rule, dcl.ambig.res.cpp.  The first alternative
 * passes its unary-expression on.
 */
node_index parser::cast_expression()
{
    /* as for assignment-expression: here for the casts and unary
       operators, which nest without one */
    nesting_level level(*this);
    if (!nesting_within_limit("an expression nested no deeper than this "
                              "implementation's limit"))
        return 0;
    if (!at(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return unary_expression();
    size_t mark = begin_node();
    token_index first = position;
    if (!parenthesized_type_id(true)) {
        if (failed())
            return 0;
        return unary_expression();
    }
    cast_expression();
    if (failed())
        return 0;
    return finish_node(syntax_kind::cast_expression, mark, first);
}

} // namespace cplusplus
