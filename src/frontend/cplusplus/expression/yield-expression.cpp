/*
 * yield-expression.cpp: [expr.yield] Yielding.
 *
 *     yield-expression:
 *         co_yield assignment-expression
 *         co_yield braced-init-list
 *
 * [expr.ass]/1 makes it one of the assignment-expressions.  Where it may
 * appear ([expr.yield]/1: "A yield-expression shall appear only within a
 * suspension context of a function") is a semantic rule.  co_yield is a
 * keyword from C++20 ([lex.key]), so nothing else checks the edition.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::yield_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* co_yield */
    if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
        if (list_initialization_missing())
            return 0;
        braced_init_list();
    } else {
        assignment_expression();
    }
    if (failed())
        return 0;
    return finish_node(syntax_kind::yield_expression, mark, first);
}

} // namespace cplusplus
