/*
 * pointer-to-member-conversions.cpp: [conv.mem] Pointer-to-member conversions:
 * /1 the null member pointer conversion, /2 a pointer to member of a base class
 * to one of the derived class; and the conversion the other way that
 * [expr.static.cast]/12 allows, which shares the path.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

bool semantic_analysis::is_member_pointer(qualified_type t) const
{
    return type_table[t.unqualified()].kind == type_kind::pointer_to_member;
}

bool semantic_analysis::is_member_function_pointer(qualified_type t) const
{
    const struct type &r = type_table[t.unqualified()];
    return r.kind == type_kind::pointer_to_member &&
           type_table[r.operand.unqualified()].kind == type_kind::function;
}

/*
 * [conv.mem]/1: "A null pointer constant ([conv.ptr]) can be converted to a
 * pointer-to-member type; the result is the null member pointer value of
 * that type and is distinguishable from any pointer to member not created
 * from a null pointer constant."  /2: "A prvalue of type "pointer to member
 * of B of type cv T", where B is a class type, can be converted to a
 * prvalue of type "pointer to member of D of type cv T", where D is a
 * complete class derived ([class.derived]) from B."  Then, or alone,
 * [conv.qual]/3 (member_pointer_qualification).
 */
analysis_result semantic_analysis::member_pointer_conversion(
    SyntaxRef at, expression_meaning &m, qualified_type to, bool &handled)
{
    handled = false;
    to = qualified_type::make(to.unqualified(), cv_none);
    const struct type target = type_table[to.unqualified()];
    if (target.kind != type_kind::pointer_to_member)
        return analysis_result::well_formed;
    handled = true;
    const char *rule = "a standard conversion sequence converts the "
                       "initializer expression to the cv-unqualified "
                       "destination type, else the initialization is "
                       "ill-formed";
    if (m.zero_literal ||
        fundamental_of(m.type) == fundamental_type::std_nullptr_t) {
        null_pointer_constant_to(m, to, conversion_kind::null_member_pointer);
        return analysis_result::well_formed;
    }
    const struct type source = type_table[m.type.unqualified()];
    if (source.kind != type_kind::pointer_to_member) {
        if (source.kind == type_kind::fundamental &&
            !(is_integral(m.type) && m.evaluation != constness::not_constant))
            return ill_formed(at, rule, "dcl.init");
        return not_supported(at, "conv.mem");
    }
    if (m.type.unqualified() == to.unqualified()) {
        m.type = to;
        return analysis_result::well_formed;
    }
    entity_index b = source.entity, d = target.entity;
    if (b == d) {
        if (qualification_convertible(m.type, to)) {
            member_pointer_qualification(m, to);
            return analysis_result::well_formed;
        }
        if (similar(m.type, to))
            return ill_formed(at, rule, "dcl.init");
        return not_supported(at, "conv.mem");
    }
    qualified_type via = qualified_type::make(
        type_table.pointer_to_member(d, source.operand), cv_none);
    if ((via.unqualified() != to.unqualified() &&
         !qualification_convertible(via, to)) ||
        entity_table[d].kind != entity_kind::class_type ||
        entity_table[b].kind != entity_kind::class_type ||
        !entity_table[d].size)
        return not_supported(at, "conv.mem");
    if (!base_subobjects(d, b))
        return ill_formed(at,
                          "a pointer to member of B converts to a pointer to "
                          "member of D, where D is a complete class derived "
                          "from B",
                          "conv.mem");
    /* "If B is an inaccessible ([class.access]), ambiguous
       ([class.member.lookup]), or virtual ([class.mi]) base class of D, or
       a base class of a virtual base class of D, a program that
       necessitates this conversion is ill-formed." */
    if (base_subobjects(d, b) != 1 || !nonvirtual_subobjects(d, b))
        return ill_formed(at,
                          "if B is an ambiguous or virtual base class of D, "
                          "or a base class of a virtual base class of D, a "
                          "program that necessitates this conversion is "
                          "ill-formed",
                          "conv.mem");
    analysis_result r =
        member_pointer_along_path(at.first_token(), d, b, via, false, true, m);
    if (r == analysis_result::well_formed)
        member_pointer_qualification(m, to);
    return r;
}

/*
 * [conv.qual]/3: "A prvalue of type T1 can be converted to type T2 if the
 * cv-combined type of T1 and T2 is T2"; /4: "A prvalue of type "pointer to
 * member of X of type cv1 T" can be converted to a prvalue of type "pointer
 * to member of X of type cv2 T" if "cv2 T" is more cv-qualified than "cv1
 * T"": the same value.  The caller has checked the conversion.
 */
void semantic_analysis::member_pointer_qualification(expression_meaning &m,
                                                     qualified_type to)
{
    to = qualified_type::make(to.unqualified(), cv_none);
    if (m.type.unqualified() == to.unqualified())
        return;
    m.type = to;
    converted(m, conversion_kind::member_pointer_qualification);
}

/*
 * [abi:data-member-pointers]: "Base-to-derived and derived-to-base
 * conversions of a non-null data member pointer can be performed by adding
 * or subtracting (respectively) the static offset of the base within the
 * derived class."  [abi:member-function-pointers]: "by adding or
 * subtracting (respectively) the static offset of the base within the
 * derived class to the stored this-adjustment value."  One conversion, the
 * offsets of the steps on the non-virtual path summed (base-clause.cpp's
 * path, which pointers to classes take too).
 */
analysis_result semantic_analysis::member_pointer_along_path(
    token_index at, entity_index derived, entity_index base,
    qualified_type type, bool to_base, bool access, expression_meaning &m)
{
    uint64_t offset = 0;
    owned_array<entity_index> steps;
    analysis_result r =
        accessible_path(at, derived, base, access, "conv.mem", steps);
    if (r != analysis_result::well_formed)
        return r;
    for (entity_index step : steps)
        offset += entity_table[step].offset;
    m.type = qualified_type::make(type.unqualified(), cv_none);
    if (m.evaluation == constness::constant)
        m.evaluation = constness::unknown;
    conversion_kind kind = conversion_kind::member_pointer_to_derived;
    if (to_base)
        kind = conversion_kind::member_pointer_to_base;
    converted(m, kind);
    if (m.node)
        semantics[m.node].value = offset;
    return analysis_result::well_formed;
}

} // namespace cplusplus
