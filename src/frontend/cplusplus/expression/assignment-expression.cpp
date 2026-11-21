/*
 * assignment-expression.cpp: [expr.ass] Assignment and compound assignment
 * operators, with [expr.cond] Conditional operator, whose production is the
 * other half of assignment-expression.
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
    if (at(preprocessing_op_or_punc::COLON)) {
        /* [ext:gnu_conditional_omitted] a ? : c */
        if (tokens.files.behavior.has(extension::gnu_conditional_omitted))
            return gnu_conditional_omitted(mark, first, conditional);
        return beyond_this_step();
    }
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
 * yield-expression.cpp's and throw-expression.cpp's.
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
analysis_result semantic_analysis::assignment(SyntaxRef n,
                                              expression_meaning &m)
{
    if (n.size() != 3 || !n[1].is_token())
        return not_supported(n, "expr.ass");
    preprocessing_op_or_punc op =
        preprocessing_op_or_punc_primary(tokens[n[1].token()].op_or_punc);
    expression_meaning left, right;
    /* [ext:clang_atomic] an atomic object is assigned (written with one
       atomic store or read-modify-write, C's code generator) */
    atomic_object_operand = true;
    analysis_result r = expression(n[0], left);
    if (r != analysis_result::well_formed)
        return r;
    if (n[2].is(syntax_kind::braced_init_list)) {
        /* [expr.ass]/8: for an aggregate with only trivial assignment,
           the list initializes the object passed to that operator. */
        const struct type target = type_table[left.type.unqualified()];
        if (op != preprocessing_op_or_punc::EQUAL ||
            target.kind != type_kind::class_type ||
            !is_aggregate(target.entity) || !assigned_bitwise(target.entity))
            return not_supported(n, "expr.ass");
        right.type = qualified_type::make(left.type.unqualified(), cv_none);
        initialized_value value;
        VTRY_SET(r, list_initialization(n[2], right.type, false, value));
        right.category = value_category::prvalue;
        right.node = value.node;
        return class_assignment(n, op, left, right, m);
    }
    /* [over.over]/1: "the left side of an assignment" */
    if (op == preprocessing_op_or_punc::EQUAL)
        r = target_expression(n[2], left.type, right);
    else
        VTRY_SET(r, expression(n[2], right));
    if (r != analysis_result::well_formed)
        return r;
    return assignment_of(n, n[1].token(), op, n[0], n[2], left, right, m);
}

/* [expr.ass] of the analysed operands: the operator function the overload
   rules select, or the built-in assignment (the traits ask it of declval
   operands too) */
analysis_result semantic_analysis::assignment_of(
    SyntaxRef n, token_index at, preprocessing_op_or_punc op,
    SyntaxElement left_syntax, SyntaxElement right_syntax,
    expression_meaning &left, expression_meaning &right, expression_meaning &m)
{
    analysis_result r = analysis_result::well_formed;
    /* [over.match.oper]/2: an operand of class or enumeration type */
    bool handled = false;
    r = binary_operator_function(n, at, op, left_syntax, left, right_syntax,
                                 right, handled, m);
    if (r != analysis_result::well_formed || handled)
        return r;
    /* [ext:clang_atomic] an atomic object is assigned a value of its value
       type */
    qualified_type assigned_type = left.type;
    if (type_index value = atomic_value_type(left.type))
        assigned_type = qualified_type::make(value, left.type.cv());
    const struct type record = type_table[assigned_type.unqualified()];
    if (record.kind == type_kind::class_type)
        return class_assignment(n, op, left, right, m);
    if (record.kind != type_kind::fundamental &&
        record.kind != type_kind::pointer &&
        !(record.kind == type_kind::pointer_to_member &&
          op == preprocessing_op_or_punc::EQUAL) &&
        record.kind != type_kind::vector &&
        !(record.kind == type_kind::enumeration &&
          enumeration_of(assigned_type)))
        return not_supported(n, "expr.ass");
    /* [basic.lval]/10: "An lvalue is modifiable unless its type is
       const-qualified or is a function type." */
    if (left.category != value_category::lvalue || (left.type.cv() & cv_const))
        return ill_formed(n,
                          "the assignment operators require a modifiable "
                          "lvalue as their left operand",
                          "expr.ass");
    VTRY_SET(r, operand_value(n, right));
    qualified_type target =
        qualified_type::make(assigned_type.unqualified(), cv_none);
    /* E1 op= E2: the type E1 op E2 is computed in */
    qualified_type computation = target;
    if (op == preprocessing_op_or_punc::EQUAL) {
        /* [expr.ass]/3: "If the right operand is an expression, it is
           implicitly converted to the cv-unqualified type of the left
           operand." */
        VTRY_SET(r, implicit_conversion(n, right, target, false));
    } else {
        /* [expr.ass]/6: "The behavior of an expression of the form E1 op= E2
           is equivalent to E1 = E1 op E2 except that E1 is evaluated only
           once. ... For += and -=, E1 shall either have arithmetic type or
           be a pointer to a possibly cv-qualified completely-defined object
           type.  In all other cases, E1 shall have arithmetic type." */
        preprocessing_op_or_punc arithmetic_op =
            compound_assignment_operator(op);
        if (arithmetic_op == preprocessing_op_or_punc::count)
            return not_supported(n, "expr.ass");
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
        if (is_pointer(assigned_type)) {
            if (!additive)
                return ill_formed(n,
                                  "in all cases other than += and -=, E1 "
                                  "shall have arithmetic type",
                                  "expr.ass");
            if (!is_object_pointer(assigned_type) ||
                !size_of_type(type_table[assigned_type.unqualified()].operand))
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
            if (r == analysis_result::well_formed &&
                record.kind == type_kind::vector)
                /* [ext:gnu_vector_size] E1 = E1 op E2 lane by lane */
                r = vector_binary(n, at, arithmetic_op, value, right, result);
            else if (r == analysis_result::well_formed)
                r = arithmetic(n, arithmetic_op, value, right, result);
            computation = result.type;
            if (r == analysis_result::well_formed)
                VTRY_SET(r, implicit_conversion(n, result, target, false));
        }
    }
    uint32_t assigned = left.node;
    m = left;
    m.evaluation = constness::not_constant;
    m.entity = 0;
    m.beyond_integral_constant = true;
    m.node = expression_node(semantic_kind::assignment, m, at, assigned,
                             right.node, static_cast<uint8_t>(op));
    semantics[m.node].computation = computation;
    return analysis_result::well_formed;
}

/*
 * Whether the implicit copy assignment operator of class c, whose subobjects
 * are assigned bitwise (assigned_bitwise), is deleted: [class.copy.assign]/2
 * "If the class definition declares a move constructor or move assignment
 * operator, the implicitly declared copy assignment operator is defined as
 * deleted"; /7 "a non-static data member of reference type", or a
 * subobject whose copy assignment is deleted (/7.4: overload resolution
 * finds a deleted function); a closure type's ([expr.prim.lambda.closure]/13).
 */
bool semantic_analysis::copy_assignment_deleted(entity_index c) const
{
    if (declares_move_constructor(c) ||
        entity_table[c].closure_assignment_deleted)
        return true;
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.member_count; i++) {
        type_kind k = type_table[entity_table[class_members[x.first_member + i]]
                                     .type.unqualified()]
                          .kind;
        if (k == type_kind::lvalue_reference ||
            k == type_kind::rvalue_reference)
            return true;
    }
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes)
        if (entity_table[k].kind == entity_kind::class_type &&
            copy_assignment_deleted(k))
            return true;
    return false;
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
analysis_result semantic_analysis::class_assignment(SyntaxRef n,
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
    /* [class.copy.assign]/1: the copy assignment operator's parameter is
       "X, const X&, ..." and the move's "X&&": a right operand of another
       type is converted to the class ([over.match.oper]/3.3 with
       [over.best.ics]) by a converting constructor or conversion function
       ([over.ics.user]; an explicit constructor does not take part), or is
       an object of a derived class, bound to the base's parameter
       ([over.best.ics]/6); the temporary is assigned, which for the
       bitwise-assigned classes below has the effect of the selected
       operator.  A class with a user-declared assignment operator is
       binary_operator_function's. */
    if (right.type.unqualified() != left.type.unqualified()) {
        if (!assigned_bitwise(c))
            return not_supported(n, "over.match.oper");
        initialized_value converted;
        VTRY(r, initialize_with(
                    n, right,
                    qualified_type::make(left.type.unqualified(), cv_none),
                    false, false, converted));
        right.type = qualified_type::make(left.type.unqualified(), cv_none);
        right.category = value_category::prvalue;
        right.node = converted.node;
    }
    /* [class.copy.assign]/12: the implicitly-defined copy assignment
       operator "performs memberwise copy assignment of its subobjects":
       one that calls an operator function is not analysed */
    if (!assigned_bitwise(c))
        return not_supported(n, "class.copy.assign");
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.member_count; i++)
        if (cv_of(type_table,
                  entity_table[class_members[x.first_member + i]].type) &
            cv_const)
            return not_supported(n, "class.copy.assign");
    if (copy_assignment_deleted(c)) {
        /* [impl.clang] -fms-compatibility keeps the copy assignment
           operator beside a user-declared move constructor */
        if (tokens.files.behavior.microsoft_compatibility)
            return not_supported(n, "class.copy.assign");
        return ill_formed(n,
                          "the implicitly-declared copy assignment operator "
                          "is defined as deleted",
                          "class.copy.assign");
    }
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
    VTRY(r, operand_value(n, right));
    uint32_t assigned = left.node;
    m = left;
    m.evaluation = constness::not_constant;
    m.entity = 0;
    m.beyond_integral_constant = true;
    m.node = expression_node(semantic_kind::assignment, m, n.first_token(),
                             assigned, right.node, static_cast<uint8_t>(op));
    semantics[m.node].computation = left.type;
    return analysis_result::well_formed;
}

} // namespace cplusplus
