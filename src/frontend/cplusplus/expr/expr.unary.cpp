/*
 * expr.unary.cpp: [expr.unary] Unary expressions, with [expr.unary.op],
 * [expr.pre.incr], [expr.sizeof], [expr.alignof] and [expr.unary.noexcept],
 * which are alternatives of the one production or a production of one line.
 *
 *     unary-expression:
 *         postfix-expression
 *         unary-operator cast-expression
 *         ++ cast-expression
 *         -- cast-expression
 *         await-expression
 *         sizeof unary-expression
 *         sizeof ( type-id )
 *         sizeof ... ( identifier )
 *         alignof ( type-id )
 *         noexcept-expression
 *         new-expression
 *         delete-expression
 *
 *     unary-operator: one of
 *         * & + - ! ~
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

bool is_unary_operator(const token &t)
{
    if (t.kind != token_kind::operator_or_punctuator)
        return false;
    switch (preprocessing_op_or_punc_primary(t.op_or_punc)) {
    case preprocessing_op_or_punc::STAR:
    case preprocessing_op_or_punc::AMPERSAND:
    case preprocessing_op_or_punc::PLUS:
    case preprocessing_op_or_punc::MINUS:
    case preprocessing_op_or_punc::EXCLAMATION:
    case preprocessing_op_or_punc::TILDE:
        return true;
    default:
        return false;
    }
}

} // namespace

/*
 * [expr.unary]/1: "Expressions with unary operators group right-to-left":
 * the operand of an operator is a cast-expression.
 *
 * [expr.unary.op]/10: "There is an ambiguity in the grammar when ~ is
 * followed by a type-name or decltype-specifier. The ambiguity is resolved
 * by treating ~ as the unary complement operator rather than as the start
 * of an unqualified-id naming a destructor."  So '~' here is always the
 * operator.
 *
 * [expr.sizeof]/1: "The operand is either an expression, which is an
 * unevaluated operand, or a parenthesized type-id"; which of the two a '('
 * begins is [dcl.ambig.res]/2's rule.  [expr.alignof]/1: "The operand shall
 * be a type-id".
 *
 * await-expression is P6's and sizeof ... ( identifier ) P7's.  The first
 * alternative passes its postfix-expression on.
 */
node_index parser::unary_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    if (is_unary_operator(peek()) || at(preprocessing_op_or_punc::PLUS_PLUS) ||
        at(preprocessing_op_or_punc::MINUS_MINUS)) {
        add_token();
        cast_expression();
        if (failed())
            return 0;
        return finish_node(syntax_kind::unary_expression, mark, first);
    }
    if (at(standard_keyword::sizeof_)) {
        add_token();
        if (at(preprocessing_op_or_punc::ELLIPSIS))
            return beyond_this_step();
        bool type = false;
        if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
            type = parenthesized_type_id(false);
            if (failed())
                return 0;
        }
        if (!type) {
            unary_expression();
            if (failed())
                return 0;
        }
        return finish_node(syntax_kind::unary_expression, mark, first);
    }
    if (at(standard_keyword::alignof_)) {
        add_token();
        if (!at(preprocessing_op_or_punc::LEFT_PARENTHESIS))
            return expected("'(' after alignof", "expr.alignof");
        if (!parenthesized_type_id(false)) {
            if (failed())
                return 0;
            /* [ext.gnu] alignof of an expression: P9 */
            return beyond_this_step();
        }
        return finish_node(syntax_kind::unary_expression, mark, first);
    }
    if (at(standard_keyword::noexcept_))
        return noexcept_expression();
    if (at(standard_keyword::new_) ||
        (at(preprocessing_op_or_punc::COLON_COLON) &&
         at(standard_keyword::new_, 1)))
        return new_expression();
    if (at(standard_keyword::delete_) ||
        (at(preprocessing_op_or_punc::COLON_COLON) &&
         at(standard_keyword::delete_, 1)))
        return delete_expression();
    if (at(standard_keyword::co_await_))
        return beyond_this_step();
    return postfix_expression();
}

/*
 * [expr.unary.noexcept]:
 *
 *     noexcept-expression:
 *         noexcept ( expression )
 *
 * [expr.unary.noexcept]/1: "The noexcept operator determines whether the
 * evaluation of its operand, which is an unevaluated operand, can throw an
 * exception".
 */
node_index parser::noexcept_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token();
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after noexcept", "expr.unary.noexcept");
    expression();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to close the operand of noexcept",
                        "expr.unary.noexcept");
    return finish_node(syntax_kind::noexcept_expression, mark, first);
}

} // namespace cplusplus
