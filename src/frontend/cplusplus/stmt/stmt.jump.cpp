/*
 * stmt.jump.cpp: [stmt.jump] Jump statements, with [stmt.break],
 * [stmt.cont], [stmt.return] and [stmt.goto].
 *
 *     jump-statement:
 *         break ;
 *         continue ;
 *         return expr-or-braced-init-list_opt ;
 *         coroutine-return-statement
 *         goto identifier ;
 *
 * coroutine-return-statement is P6's.
 */
#include "stmt.ambig.h"

namespace cplusplus
{

node_index parser::jump_statement()
{
    size_t mark = begin_node();
    token_index first = position;
    standard_keyword k = keyword_of(peek());
    add_token();
    if (k == standard_keyword::return_ &&
        !at(preprocessing_op_or_punc::SEMICOLON)) {
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
    } else if (k == standard_keyword::goto_) {
        if (at(preprocessing_op_or_punc::STAR))
            /* [ext.gnu] a computed goto, goto *p: P9 */
            return beyond_this_step();
        if (!at_identifier())
            return expected("an identifier after goto", "stmt.goto");
        add_token();
    }
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the jump-statement", "stmt.jump");
    return finish_node(syntax_kind::jump_statement, mark, first);
}

} // namespace cplusplus
