/*
 * expr.prim.cpp: [expr.prim] Primary expressions, with [expr.prim.literal],
 * [expr.prim.this] and [expr.prim.paren]; the names are expr.prim.id.cpp.
 *
 *     primary-expression:
 *         literal
 *         this
 *         ( expression )
 *         id-expression
 *         lambda-expression
 *         fold-expression
 *         requires-expression
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * A literal, this and an identifier are their token; a qualified-id is
 * its node; the third alternative is the primary-expression node.
 * [expr.prim.paren]/1: "A parenthesized expression (E) is a primary
 * expression whose type, value, and value category are identical to those
 * of E."
 *
 * lambda-expression is expr.prim.lambda.cpp's, fold-expression and requires-expression P7's.
 */
node_index parser::primary_expression()
{
    /* a literal ([lex.literal.kinds]; true, false and nullptr are keywords
       that are literals) */
    if (peek().literal != literal_kind::none || at(standard_keyword::this_)) {
        add_token();
        return 0;
    }
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        if (at(preprocessing_op_or_punc::LEFT_BRACE, 1))
            /* [ext.gnu] a statement expression, ({ ... }): P9 */
            return beyond_this_step();
        if (at(preprocessing_op_or_punc::ELLIPSIS, 1))
            return beyond_this_step(); /* fold-expression */
        size_t mark = begin_node();
        token_index first = position;
        add_token();
        expression();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("')' to close the parenthesized expression",
                            "expr.prim.paren");
        return finish_node(syntax_kind::primary_expression, mark, first);
    }
    if (at(preprocessing_op_or_punc::LEFT_BRACKET)) {
        /* [dcl.attr.grammar]/7: [[ only for an attribute-specifier, and no
           expression begins with one */
        if (at(preprocessing_op_or_punc::LEFT_BRACKET, 1))
            return misplaced_attribute_brackets();
        return lambda_expression();
    }
    if (at_identifier() || at(preprocessing_op_or_punc::COLON_COLON) ||
        at(standard_keyword::decltype_) || at(standard_keyword::operator_))
        return id_expression(false);
    switch (keyword_of(peek())) {
    case standard_keyword::requires_:
    /* an extension's keyword (__builtin_va_arg, __is_same, ...): P9 */
    case standard_keyword::template_:
    case standard_keyword::extension:
        return beyond_this_step();
    default:
        break;
    }
    return expected("an expression", "expr.prim");
}

} // namespace cplusplus
