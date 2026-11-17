/*
 * stmt.expr.cpp: [stmt.expr] Expression statement.
 *
 *     expression-statement:
 *         expression_opt ;
 *
 * [stmt.expr]/1: "An expression statement with the expression missing is
 * called a null statement."
 */
#include "frontend/cplusplus/sema.h"
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

/* ---- semantic analysis ---------------------------------------------- */

/* [stmt.expr]/1: "The expression is a discarded-value expression"; an
   expression-statement without the expression is a null statement */
analysis_result semantic_analysis::expression_statement(node_index n,
                                                        uint32_t &built)
{
    const syntax_node &node = tree[n];
    if (node.child_count == 1) {
        built = statement_node(semantic_kind::null_statement, node.first_token);
        return analysis_result::well_formed;
    }
    if (node.child_count != 2)
        return not_supported(n, "stmt.expr");
    expression_meaning m;
    analysis_result r = expression(tree.children(n)[0], m);
    built = statement_node(semantic_kind::expression_statement,
                           node.first_token, m.node);
    return r;
}

} // namespace cplusplus
