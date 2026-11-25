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
 * rethrows.  /3: "Evaluating a throw-expression with an operand throws an
 * exception object ... The type of the exception object is determined by
 * removing any top-level cv-qualifiers from the static type of the operand
 * and adjusting the type from 'array of T' or function type T to 'pointer
 * to T'"; the object is "copy-initialized from the operand".  Not
 * analysed: [except.throw]/5's check of the destructor.
 */
analysis_result semantic_analysis::throw_expression(ThrowExpression n,
                                                    expression_meaning &m)
{
    uint32_t initialization = 0;
    qualified_type object;
    if (n.size() > 1) {
        expression_meaning operand;
        VTRY(r, expression(n[1], operand));
        object = decayed(operand.type);
        const struct type &k = type_table[object.unqualified()];
        if (k.kind == type_kind::fundamental &&
            k.fundamental == fundamental_type::void_)
            return ill_formed(n, "cannot throw an object of type void",
                              "expr.throw");
        if (k.kind == type_kind::pointer &&
            type_table[k.operand.unqualified()].kind == type_kind::class_type &&
            !require_complete(k.operand))
            return ill_formed(n,
                              "cannot throw a pointer to an object of "
                              "incomplete type",
                              "except.throw");
        if (k.kind == type_kind::class_type) {
            if (!require_complete(object))
                return ill_formed(n,
                                  "cannot throw an object of incomplete type",
                                  "expr.throw");
            /* [class.copy.elision]/3: "An implicitly movable entity ... If the expression in a throw-expression
               is an id-expression that names an implicitly movable entity ... overload resolution to select the
               constructor for the copy is first performed as if the expression were an rvalue" */
            entity_index movable = implicitly_movable(n[1]);
            if (movable && movable >= try_entities &&
                operand.category == value_category::lvalue &&
                operand.type.unqualified() == object.unqualified() &&
                declares_move_constructor(k.entity))
                operand.category = value_category::xvalue;
        } else {
            VTRY_SET(r, operand_value(n, operand));
        }
        /* the object's memory, which the initialization is of */
        expression_meaning pointer;
        pointer.type =
            qualified_type::make(type_table.pointer_to(object), cv_none);
        pointer.category = value_category::prvalue;
        pointer.evaluation = constness::not_constant;
        pointer.node =
            expression_node(semantic_kind::allocated, pointer, n.first_token());
        expression_meaning place;
        place.type = object;
        place.category = value_category::lvalue;
        place.evaluation = constness::not_constant;
        place.node = expression_node(semantic_kind::indirection, place,
                                     n.first_token(), pointer.node);
        initialized_value value;
        VTRY_SET(r, initialize_with(n, operand, object, false, false, value));
        if (value.node) {
            semantic_node &v = semantics[value.node];
            if (v.kind == semantic_kind::construct)
                v.a = place.node;
            else if (k.kind == type_kind::class_type &&
                     v.kind != semantic_kind::initializer_list &&
                     v.kind != semantic_kind::call &&
                     !(v.kind == semantic_kind::conversion &&
                       v.op == static_cast<uint8_t>(
                                   conversion_kind::lvalue_to_rvalue)))
                /* a prvalue of the class, stored in the object it
                   initializes: not lowered */
                return not_supported_because(
                    n, "expr.throw",
                    "a throw operand not initialized by a constructor or a "
                    "plain copy");
        }
        initialization = value.node;
    }
    m = expression_meaning();
    m.type = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_none);
    m.category = value_category::prvalue;
    m.evaluation = constness::not_constant;
    m.node = expression_node(semantic_kind::throw_exception, m, n.first_token(),
                             initialization);
    semantics[m.node].computation = object;
    if (n.size() > 1)
        semantics[m.node].value = 1;
    return analysis_result::well_formed;
}

} // namespace cplusplus
