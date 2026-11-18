/*
 * expr.mptr.oper.cpp: [expr.mptr.oper] Pointer-to-member operators: /2
 * E1.*E2, /3 E1->*E2, /5 the cv-qualifiers of the result, /6 its value
 * category and the call of a member function through one.  (The syntax,
 * pm-expression, is expr.compound.cpp's.)
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
    node_index n, token_index at, bool arrow, expression_meaning &left,
    expression_meaning &right, expression_meaning &m)
{
    analysis_result r = operand_value(n, right);
    if (r != analysis_result::well_formed)
        return r;
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
        r = operand_value(n, object);
        if (r != analysis_result::well_formed)
            return r;
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
    if (!entity_table[t].size || !entity_table[u].size)
        return not_supported(n, "expr.mptr.oper");
    if (u != t) {
        if (!base_subobjects(u, t))
            return ill_formed(n,
                              "the first operand shall be of class T or of a "
                              "class of which T is an unambiguous and "
                              "accessible base class",
                              "expr.mptr.oper");
        r = implicit_conversion(
            n, object,
            qualified_type::make(
                type_table.pointer_to(qualified_type::make(
                    entity_table[t].type.unqualified(), pointee.cv())),
                cv_none),
            false);
        if (r != analysis_result::well_formed)
            return r;
    }
    m = expression_meaning();
    if (function) {
        /* /5: "The restrictions on cv-qualification ... are the same as
           the rules for E1.E2 given in [expr.ref]": the member functions
           here have no cv-qualifier-seq (conv.mem.cpp), which an object
           with one may not call; /6: "The result of a .* expression whose
           second operand is a pointer to a member function is a
           prvalue." */
        if (pointee.cv())
            return not_supported(n, "expr.mptr.oper");
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
