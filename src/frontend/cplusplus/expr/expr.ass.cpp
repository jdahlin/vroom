/*
 * expr.ass.cpp: [expr.ass] Assignment and compound assignment operators,
 * with [expr.cond] Conditional operator, whose production is the other
 * half of assignment-expression.
 *
 *     assignment-expression:
 *         conditional-expression
 *         yield-expression
 *         throw-expression
 *         logical-or-expression assignment-operator initializer-clause
 *
 *     assignment-operator: one of
 *         = *= /= %= += -= >>= <<= &= ^= |=
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * [expr.cond]:
 *
 *     conditional-expression:
 *         logical-or-expression
 *         logical-or-expression ? expression : assignment-expression
 *
 * [expr.cond]/1: "Conditional expressions group right-to-left": the third
 * operand is an assignment-expression, which a conditional-expression is.
 * *conditional says which alternative matched.
 */
node_index parser::conditional_expression(bool *conditional)
{
    size_t mark = begin_node();
    token_index first = position;
    if (conditional)
        *conditional = false;
    node_index n = binary_expression(0);
    if (failed())
        return 0;
    if (!at(preprocessing_op_or_punc::QUESTION))
        return n;
    add_token();
    if (at(preprocessing_op_or_punc::COLON))
        /* [ext.gnu] a conditional with its second operand omitted: P9 */
        return beyond_this_step();
    expression();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::COLON))
        return expected("':' of the conditional-expression", "expr.cond");
    assignment_expression();
    if (failed())
        return 0;
    if (conditional)
        *conditional = true;
    return finish_node(syntax_kind::conditional_expression, mark, first);
}

/*
 * [expr.ass]/1: "The assignment operator (=) and the compound assignment
 * operators all group right-to-left": the right operand is an
 * initializer-clause, an assignment-expression or a braced-init-list.  The
 * left operand is a logical-or-expression, so a conditional-expression
 * with a '?' is not followed by an assignment-operator.
 *
 * Its other alternatives, yield-expression and throw-expression, are
 * expr.yield.cpp's and expr.throw.cpp's.
 */
node_index parser::assignment_expression()
{
    nesting_level level(*this);
    if (!nesting_within_limit("an expression nested no deeper than this "
                              "implementation's limit"))
        return 0;
    if (at(standard_keyword::throw_))
        return throw_expression();
    if (at(standard_keyword::co_yield_))
        return yield_expression();
    size_t mark = begin_node();
    token_index first = position;
    bool conditional = false;
    node_index n = conditional_expression(&conditional);
    if (failed())
        return 0;
    if (conditional || !is_assignment_operator(peek()))
        return n;
    add_token();
    if (list_initialization_missing())
        return 0;
    initializer_clause();
    if (failed())
        return 0;
    return finish_node(syntax_kind::assignment_expression, mark, first);
}

} // namespace cplusplus
