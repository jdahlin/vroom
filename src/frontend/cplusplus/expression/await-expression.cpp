/*
 * await-expression.cpp: [expr.await] Await.
 *
 *     await-expression:
 *         co_await cast-expression
 *
 * [expr.unary]/1 makes it one of the unary-expressions.  [expr.await]/2:
 * "An await-expression shall appear only in a potentially-evaluated
 * expression within the compound-statement of a function-body outside of a
 * handler": a semantic rule.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::await_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* co_await */
    cast_expression();
    if (failed())
        return 0;
    return finish_node(syntax_kind::await_expression, mark, first);
}

} // namespace cplusplus
