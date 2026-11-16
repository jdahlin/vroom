/*
 * stmt.return.coroutine.cpp: [stmt.return.coroutine] The co_return
 * statement.
 *
 *     coroutine-return-statement:
 *         co_return expr-or-braced-init-list_opt ;
 *
 * [stmt.jump]/1 makes it a jump-statement.  co_return is a keyword from
 * C++20 ([lex.key]).
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::coroutine_return_statement()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* co_return */
    if (!at(preprocessing_op_or_punc::SEMICOLON)) {
        /* expr-or-braced-init-list ([dcl.init]) */
        if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
            if (list_initialization_missing())
                return 0;
            braced_init_list();
        } else {
            expression();
        }
        if (failed())
            return 0;
    }
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the coroutine-return-statement",
                        "stmt.return.coroutine");
    return finish_node(syntax_kind::coroutine_return_statement, mark, first);
}

} // namespace cplusplus
