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
#include "frontend/cplusplus/sema.h"

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

/* ---- semantic analysis ---------------------------------------------- */

/*
 * [expr.ass]/1: "The assignment operator (=) and the compound assignment
 * operators all group right-to-left.  All require a modifiable lvalue as
 * their left operand; their result is an lvalue referring to the left
 * operand."  The left operand is modified: no constant expression here
 * ([expr.const]/5.16).
 */
analysis_result semantic_analysis::assignment(node_index n,
                                              expression_meaning &m)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    if (node.child_count != 3 || !children[1].is_token() ||
        syntax_is(tree, children[2], syntax_kind::braced_init_list))
        return not_supported(n, "expr.ass");
    preprocessing_op_or_punc op = preprocessing_op_or_punc_primary(
        tokens[children[1].index()].op_or_punc);
    expression_meaning left, right;
    analysis_result r = expression(children[0], left);
    if (r == analysis_result::well_formed)
        r = expression(children[2], right);
    if (r != analysis_result::well_formed)
        return r;
    if (op != preprocessing_op_or_punc::EQUAL) {
        /* [over.match.oper]/2: an operand of class or enumeration type */
        owned_array<syntax_child> syntax;
        owned_array<expression_meaning> operands;
        syntax.append(children[0]);
        operands.append(left);
        syntax.append(children[2]);
        operands.append(right);
        bool handled = false;
        r = overloaded_operator(n, children[1].index(), op, false, syntax,
                                operands, handled, m);
        if (r != analysis_result::well_formed || handled)
            return r;
    }
    const struct type record = type_table[left.type.unqualified()];
    if (record.kind == type_kind::class_type)
        return class_assignment(n, op, left, right, m);
    if (record.kind != type_kind::fundamental &&
        record.kind != type_kind::pointer &&
        !(record.kind == type_kind::enumeration && enumeration_of(left.type)))
        return not_supported(n, "expr.ass");
    /* [basic.lval]/10: "An lvalue is modifiable unless its type is
       const-qualified or is a function type." */
    if (left.category != value_category::lvalue || (left.type.cv() & cv_const))
        return ill_formed(n,
                          "the assignment operators require a modifiable "
                          "lvalue as their left operand",
                          "expr.ass");
    r = operand_value(n, right);
    if (r != analysis_result::well_formed)
        return r;
    qualified_type target =
        qualified_type::make(left.type.unqualified(), cv_none);
    /* E1 op= E2: the type E1 op E2 is computed in */
    qualified_type computation = target;
    if (op == preprocessing_op_or_punc::EQUAL) {
        /* [expr.ass]/3: "If the right operand is an expression, it is
           implicitly converted to the cv-unqualified type of the left
           operand." */
        r = implicit_conversion(n, right, target, false);
        if (r != analysis_result::well_formed)
            return r;
    } else {
        /* [expr.ass]/6: "The behavior of an expression of the form E1 op= E2
           is equivalent to E1 = E1 op E2 except that E1 is evaluated only
           once. ... For += and -=, E1 shall either have arithmetic type or
           be a pointer to a possibly cv-qualified completely-defined object
           type.  In all other cases, E1 shall have arithmetic type." */
        preprocessing_op_or_punc arithmetic_op;
        switch (op) {
        case preprocessing_op_or_punc::PLUS_EQUAL:
            arithmetic_op = preprocessing_op_or_punc::PLUS;
            break;
        case preprocessing_op_or_punc::MINUS_EQUAL:
            arithmetic_op = preprocessing_op_or_punc::MINUS;
            break;
        case preprocessing_op_or_punc::STAR_EQUAL:
            arithmetic_op = preprocessing_op_or_punc::STAR;
            break;
        case preprocessing_op_or_punc::SLASH_EQUAL:
            arithmetic_op = preprocessing_op_or_punc::SLASH;
            break;
        case preprocessing_op_or_punc::PERCENT_EQUAL:
            arithmetic_op = preprocessing_op_or_punc::PERCENT;
            break;
        case preprocessing_op_or_punc::LESS_LESS_EQUAL:
            arithmetic_op = preprocessing_op_or_punc::LESS_LESS;
            break;
        case preprocessing_op_or_punc::GREATER_GREATER_EQUAL:
            arithmetic_op = preprocessing_op_or_punc::GREATER_GREATER;
            break;
        case preprocessing_op_or_punc::AMPERSAND_EQUAL:
            arithmetic_op = preprocessing_op_or_punc::AMPERSAND;
            break;
        case preprocessing_op_or_punc::CARET_EQUAL:
            arithmetic_op = preprocessing_op_or_punc::CARET;
            break;
        case preprocessing_op_or_punc::BAR_EQUAL:
            arithmetic_op = preprocessing_op_or_punc::BAR;
            break;
        default:
            return not_supported(n, "expr.ass");
        }
        bool additive = arithmetic_op == preprocessing_op_or_punc::PLUS ||
                        arithmetic_op == preprocessing_op_or_punc::MINUS;
        /* [expr.ass]/6: "E1 shall have arithmetic type": an enumeration
           has none */
        if (record.kind == type_kind::enumeration)
            return ill_formed(n,
                              "in all cases other than += and -=, E1 shall "
                              "have arithmetic type",
                              "expr.ass");
        promote_enumeration(right);
        if (is_pointer(left.type)) {
            if (!additive)
                return ill_formed(n,
                                  "in all cases other than += and -=, E1 "
                                  "shall have arithmetic type",
                                  "expr.ass");
            if (!is_object_pointer(left.type) ||
                !size_of_type(type_table[left.type.unqualified()].operand))
                return not_supported(n, "expr.ass");
            if (!is_integral(right.type)) {
                if (fundamental_of(right.type) == fundamental_type::none &&
                    !is_pointer(right.type))
                    return not_supported(n, "expr.ass");
                return ill_formed(n,
                                  "for += and -= of a pointer, E2 shall have "
                                  "integral or unscoped enumeration type",
                                  "expr.add");
            }
        } else {
            expression_meaning value = left, result;
            r = operand_value(n, value);
            if (r == analysis_result::well_formed)
                r = arithmetic(n, arithmetic_op, value, right, result);
            computation = result.type;
            if (r == analysis_result::well_formed)
                r = implicit_conversion(n, result, target, false);
            if (r != analysis_result::well_formed)
                return r;
        }
    }
    uint32_t assigned = left.node;
    m = left;
    m.evaluation = constness::not_constant;
    m.entity = 0;
    m.beyond_integral_constant = true;
    m.node = expression_node(semantic_kind::assignment, m, children[1].index(),
                             assigned, right.node, static_cast<uint8_t>(op));
    semantics[m.node].computation = computation;
    return analysis_result::well_formed;
}

/*
 * [expr.ass]/4: "If the left operand is of class type, the class shall be
 * complete.  Assignment to objects of a class is defined by the copy/move
 * assignment operator ([class.copy.assign], [over.ass])."  The
 * implicitly-declared copy assignment operator ([class.copy.assign]/2) of
 * the classes analysis has is trivial ([class.copy.assign]/9): it copies
 * the object representation; /7: it is defined as deleted if the class has
 * "a non-static data member of const non-class type (or array thereof)".
 * Its result is the left operand, an lvalue ([class.copy.assign]/12:
 * "returns an lvalue reference to the object assigned to").
 */
analysis_result semantic_analysis::class_assignment(node_index n,
                                                    preprocessing_op_or_punc op,
                                                    expression_meaning &left,
                                                    expression_meaning &right,
                                                    expression_meaning &m)
{
    if (op != preprocessing_op_or_punc::EQUAL)
        return not_supported(n, "over.match.oper");
    entity_index c = type_table[left.type.unqualified()].entity;
    if (entity_table[c].kind != entity_kind::class_type)
        return not_supported(n, "class.pre");
    if (!entity_table[c].size)
        return ill_formed(n,
                          "if the left operand is of class type, the class "
                          "shall be complete",
                          "expr.ass");
    if (right.type.unqualified() != left.type.unqualified())
        return not_supported(n, "over.match.oper");
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.member_count; i++)
        if (cv_of(type_table,
                  entity_table[class_members[x.first_member + i]].type) &
            cv_const)
            return not_supported(n, "class.copy.assign");
    /* [over.match.viable]: the implicit object parameter of the copy
       assignment operator, which is not const, binds only to a non-const
       object; an rvalue binds to it too ([over.match.funcs]/5) */
    if (left.type.cv() & cv_const)
        return ill_formed(n,
                          "the copy assignment operator of a class cannot be "
                          "called on a const object",
                          "over.match.viable");
    if (left.category != value_category::lvalue ||
        (left.type.cv() & cv_volatile))
        return not_supported(n, "class.copy.assign");
    analysis_result r = operand_value(n, right);
    if (r != analysis_result::well_formed)
        return r;
    uint32_t assigned = left.node;
    m = left;
    m.evaluation = constness::not_constant;
    m.entity = 0;
    m.beyond_integral_constant = true;
    m.node = expression_node(semantic_kind::assignment, m, tree[n].first_token,
                             assigned, right.node, static_cast<uint8_t>(op));
    semantics[m.node].computation = left.type;
    return analysis_result::well_formed;
}

} // namespace cplusplus
