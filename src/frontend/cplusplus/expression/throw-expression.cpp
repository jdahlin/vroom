/*
 * throw-expression.cpp: [expr.throw] Throwing an exception.
 *
 *     throw-expression:
 *         throw assignment-expression_opt
 *
 * [expr.ass]/1 makes it one of the assignment-expressions.  The operand is
 * absent when what follows cannot begin an assignment-expression: the end
 * of the enclosing expression, statement or initializer ([expr.throw]/4: "A
 * throw-expression with no operand rethrows the currently handled
 * exception").
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

/* the tokens after which nothing of an assignment-expression follows */
bool ends_operand(parser &p)
{
    static const preprocessing_op_or_punc ends[] = {
        preprocessing_op_or_punc::SEMICOLON,
        preprocessing_op_or_punc::RIGHT_PARENTHESIS,
        preprocessing_op_or_punc::RIGHT_BRACKET,
        preprocessing_op_or_punc::RIGHT_BRACE,
        preprocessing_op_or_punc::COMMA,
        preprocessing_op_or_punc::COLON,
        preprocessing_op_or_punc::ELLIPSIS};
    if (p.at_end())
        return true;
    for (preprocessing_op_or_punc op : ends)
        if (p.at(op))
            return true;
    return false;
}

} // namespace

node_index parser::throw_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* throw */
    if (!ends_operand(*this)) {
        assignment_expression();
        if (failed())
            return 0;
    }
    return finish_node(syntax_kind::throw_expression, mark, first);
}

/*
 * [expr.throw]/1: "A throw-expression is of type void."  It is a prvalue
 * ([basic.lval]/1: a void expression is one).  /4: without an operand it
 * rethrows.  Not analysed: the copy-initialization of the exception object
 * from the operand (/3) and [except.throw]/5's check of its destructor.
 */
analysis_result semantic_analysis::throw_expression(ThrowExpression n,
                                                    expression_meaning &m)
{
    uint32_t operand_node = 0;
    if (n.size() > 1) {
        expression_meaning operand;
        VTRY(r, expression(n[1], operand));
        operand_node = operand.node;
    }
    m = expression_meaning();
    m.type = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_none);
    m.category = value_category::prvalue;
    m.evaluation = constness::not_constant;
    m.node = expression_node(semantic_kind::throw_exception, m, n.first_token(),
                             operand_node);
    return analysis_result::well_formed;
}

} // namespace cplusplus
