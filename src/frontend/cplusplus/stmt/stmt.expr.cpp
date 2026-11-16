/*
 * stmt.expr.cpp: [stmt.expr] Expression statement.
 *
 *     expression-statement:
 *         expression_opt ;
 *
 * [stmt.expr]/1: "An expression statement with the expression missing is
 * called a null statement."
 */
#include "stmt.ambig.h"

namespace cplusplus
{

node_index parser::expression_statement()
{
    size_t mark = begin_node();
    token_index first = position;
    if (!at(preprocessing_op_or_punc::SEMICOLON)) {
        expression();
        if (failed())
            return 0;
    }
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the expression", "stmt.expr");
    return finish_node(syntax_kind::expression_statement, mark, first);
}

} // namespace cplusplus
