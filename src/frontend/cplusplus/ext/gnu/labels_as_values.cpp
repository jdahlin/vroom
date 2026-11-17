/*
 * labels_as_values.cpp: [ext:gnu_labels_as_values] (GCC manual 6.3,
 * Labels as Values): "You can get the address of a label defined in the
 * current function (or a containing function) with the unary operator
 * '&&'.  The value has type void *."  "To use these values, you need to be
 * able to jump to one.  This is done with the computed goto statement":
 *
 *     unary-expression:
 *         gnu-label-address
 *
 *     gnu-label-address:
 *         && identifier
 *
 *     jump-statement:
 *         gnu-computed-goto
 *
 *     gnu-computed-goto:
 *         goto * expression ;
 *
 * && followed by an identifier is otherwise no expression's beginning.
 */
#include "../../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::gnu_label_address()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* && */
    add_token(); /* identifier */
    return finish_node(syntax_kind::gnu_label_address, mark, first);
}

/* after goto, at the '*' */
node_index parser::gnu_computed_goto(size_t mark, token_index first)
{
    add_token(); /* * */
    expression();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the computed goto", "ext.gnu");
    return finish_node(syntax_kind::gnu_computed_goto, mark, first);
}

} // namespace cplusplus
