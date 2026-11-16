/*
 * expr.delete.cpp: [expr.delete] Delete.
 *
 *     delete-expression:
 *         ::_opt delete cast-expression
 *         ::_opt delete [ ] cast-expression
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * [expr.delete]/1: "Whenever the delete keyword is immediately followed by
 * empty square brackets, it shall be interpreted as the second
 * alternative."  A '[' with anything in it begins the operand (a
 * lambda-expression).
 */
node_index parser::delete_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    accept(preprocessing_op_or_punc::COLON_COLON);
    add_token(); /* delete */
    if (at(preprocessing_op_or_punc::LEFT_BRACKET) &&
        at(preprocessing_op_or_punc::RIGHT_BRACKET, 1)) {
        add_token();
        add_token();
    }
    cast_expression();
    if (failed())
        return 0;
    return finish_node(syntax_kind::delete_expression, mark, first);
}

} // namespace cplusplus
