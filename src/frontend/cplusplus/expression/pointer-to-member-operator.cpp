/*
 * pointer-to-member-operator.cpp: [expr.mptr.oper] Pointer-to-member operators:
 * /2 E1.*E2, /3 E1->*E2, /5 the cv-qualifiers of the result, /6 its value
 * category and the call of a member function through one.  (The syntax,
 * pm-expression, is binary-expression.cpp's.)
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [expr.mptr.oper]/2: "The binary operator .* binds its second operand,
 * which shall be of type "pointer to member of T" to its first operand,
 * which shall be a glvalue of class T or of a class of which T is an
 * unambiguous and accessible base class.  The result is an object or a
 * function of the type specified by the second operand."  /3: "The
 * expression E1->*E2 is converted into the equivalent form (*(E1)).*E2."
 * Both are analysed as the object's address converted to a pointer to T
 * ([conv.ptr]/3: the base class subobject) and the pointer to member.
 */
analysis_result semantic_analysis::member_pointer_operator(
    SyntaxRef n, token_index at, bool arrow, expression_meaning &left,
    expression_meaning &right, expression_meaning &m)
{
    VTRY(r, operand_value(n, right));
    if (!is_member_pointer(right.type)) {
        if (fundamental_of(right.type) == fundamental_type::none &&
            !is_pointer(right.type))
            return not_supported(n, "expr.mptr.oper");
        return ill_formed(n,
                          "the second operand shall be of type pointer to "
                          "member of T",
                          "expr.mptr.oper");
    }
    const struct type pointer = type_table[right.type.unqualified()];
    entity_index t = pointer.entity;
    qualified_type member = pointer.operand;
    bool function =
        type_table[member.unqualified()].kind == type_kind::function;
    expression_meaning object = left;
    value_category category = value_category::lvalue;
    if (arrow) {
        VTRY_SET(r, operand_value(n, object));
        if (!is_pointer(object.type) ||
            type_table[type_table[object.type.unqualified()]
                           .operand.unqualified()]
                    .kind != type_kind::class_type) {
            if (fundamental_of(object.type) == fundamental_type::none &&
                !is_pointer(object.type))
                return not_supported(n, "expr.mptr.oper");
            return ill_formed(n,
                              "the first operand of ->* shall be of type "
                              "pointer to U where U is either T or a class "
                              "of which T is an unambiguous and accessible "
                              "base class",
                              "expr.mptr.oper");
        }
    } else {
        if (type_table[object.type.unqualified()].kind !=
            type_kind::class_type) {
            if (fundamental_of(object.type) == fundamental_type::none &&
                !is_pointer(object.type))
                return not_supported(n, "expr.mptr.oper");
            return ill_formed(n,
                              "the first operand of .* shall be a glvalue of "
                              "class T or of a class of which T is an "
                              "unambiguous and accessible base class",
                              "expr.mptr.oper");
        }
        /* [basic.lval]/7: "Whenever a prvalue appears as an operand of an
           operator that expects a glvalue for that operand, the temporary
           materialization conversion ([conv.rval]) is applied to convert
           the expression to an xvalue": the address of a prvalue is its
           temporary's (lowering's bind_reference).  One with a destructor
           is destroyed at the end of the full-expression
           ([class.temporary]/4), not analysed yet, nor an aggregate's
           temporary (aggregate_prvalue) */
        if (object.category == value_category::prvalue &&
            (aggregate_prvalue(object) ||
             entity_table[type_table[object.type.unqualified()].entity]
                 .destructor))
            return not_supported(n, "class.temporary");
        /* /6: "The result of a .* expression whose second operand is a
           pointer to a data member is an lvalue if the first operand is an
           lvalue and an xvalue otherwise." */
        category = value_category::xvalue;
        if (object.category == value_category::lvalue)
            category = value_category::lvalue;
        address_of_glvalue(at, object);
    }
    qualified_type pointee = type_table[object.type.unqualified()].operand;
    entity_index u = type_table[pointee.unqualified()].entity;
    require_complete(t);
    require_complete(u);
    if (!entity_table[t].size || !entity_table[u].size)
        return not_supported(n, "expr.mptr.oper");
    if (u != t) {
        if (!base_subobjects(u, t))
            return ill_formed(n,
                              "the first operand shall be of class T or of a "
                              "class of which T is an unambiguous and "
                              "accessible base class",
                              "expr.mptr.oper");
        VTRY_SET(r,
                 implicit_conversion(
                     n, object,
                     qualified_type::make(
                         type_table.pointer_to(qualified_type::make(
                             entity_table[t].type.unqualified(), pointee.cv())),
                         cv_none),
                     false));
    }
    m = expression_meaning();
    if (function) {
        /* /5: "The restrictions on cv-qualification, and on the value
           category of E1, are the same as the rules for E1.E2 given in
           [expr.ref]" ([expr.ref]/6.3.2.2: a function with a cv-qualifier-seq
           or ref-qualifier [class.mfct.non-static]/4, [over.match.funcs]/5:
           an object with more cv-qualifiers, an lvalue for &&, a non-lvalue
           for &, has no such call); /6: "The result of a .* expression
           whose second operand is a pointer to a member function is a
           prvalue." */
        const struct type &f = type_table[member.unqualified()];
        if (pointee.cv() & static_cast<uint32_t>(~f.function_cv) &
            (cv_const | cv_volatile))
            return ill_formed(n,
                              "a member function with fewer cv-qualifiers "
                              "than the object has cannot be called",
                              "expr.mptr.oper");
        if ((f.function_ref == 1 && category != value_category::lvalue) ||
            (f.function_ref == 2 && category == value_category::lvalue))
            return ill_formed(n,
                              "the ref-qualifier of the member function does "
                              "not match the value category of the object",
                              "expr.mptr.oper");
        m.type = member;
        m.category = value_category::prvalue;
    } else {
        /* /5 with [expr.ref]/6.2: "If E1 has type "cq1 vq1 X" and E2 has
           type "cq2 vq2 T", the expression designates ... cq12 vq12 T",
           the union of the object's and the member's (a pointer to member
           never names a mutable member's mutability: Note 1) */
        m.type = qualified_type::make(member.unqualified(),
                                      member.cv() | pointee.cv());
        m.category = category;
    }
    m.evaluation = constness::unknown;
    m.node = expression_node(semantic_kind::member_pointer_access, m, at,
                             object.node, right.node);
    return analysis_result::well_formed;
}

} // namespace cplusplus
