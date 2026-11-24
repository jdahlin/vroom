/*
 * deleted-special-members.cpp: when a defaulted special member function is
 * defined as deleted ([dcl.fct.def.delete]): [class.default.ctor]/2 the
 * default constructor, [class.copy.ctor]/6 and /10 the copy constructor,
 * [class.copy.assign]/2 and /7 the copy assignment operator.  Each rule has
 * one function here; implicit_special_members (class/constructor.cpp) and
 * implicit_assignments (class/implicit-assignment.cpp) declare the members
 * by them, and an expression that would call a deleted one is ill-formed
 * where it is ([dcl.fct.def.delete]/2).  When a member is trivial is
 * class/triviality.cpp's.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* [class.default.ctor]/2's subobjects: whether the implicit default
   constructor of class c default-constructs a subobject of class k: a base,
   or a member (or array of them) "except for a non-static data member with a
   brace-or-equal-initializer" ([class.base.init]/9.1) */
bool semantic_analysis::default_constructed(entity_index c,
                                            entity_index k) const
{
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.base_count; i++)
        if (type_table[entity_table[class_bases[x.first_base + i]]
                           .type.unqualified()]
                .entity == k)
            return true;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (m.base_subobject || m.default_initializer)
            continue;
        if (element_class(m.type) == k)
            return true;
    }
    return false;
}

/*
 * [class.dtor]/6: "A defaulted destructor for a class X is defined as
 * deleted if: X is a union-like class that has a variant member with a
 * non-trivial destructor, any potentially constructed subobject has class
 * type M (or array thereof) and M has a destructor that is deleted or
 * inaccessible".  Whether class c's destructor is deleted: by its
 * definition (= delete), or, when it is not user-provided, by a
 * subobject's (deleted or inaccessible).
 */
bool semantic_analysis::destructor_deleted(entity_index c) const
{
    const entity &x = entity_table[c];
    if (x.destructor && entity_table[x.destructor].deleted)
        return true;
    if (x.destructor && !entity_table[x.destructor].implicit)
        return false;
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes)
        if (entity_table[k].kind == entity_kind::class_type &&
            (destructor_deleted(k) || subobject_destructor_inaccessible(c, k)))
            return true;
    return false;
}

/*
 * [class.default.ctor]/2: "A defaulted default constructor for class X is
 * defined as deleted if:
 * - X is a union that has a variant member with a non-trivial default
 *   constructor and no variant member of X has a default member
 *   initializer,
 * - any non-static data member with no default member initializer is of
 *   reference type,
 * - any non-variant non-static data member of const-qualified type (or
 *   array thereof) with no brace-or-equal-initializer is not
 *   const-default-constructible,
 * - any potentially constructed subobject, except for a non-static data
 *   member with a brace-or-equal-initializer, has class type M (or array
 *   thereof) and either M has no default constructor or overload resolution
 *   as applied to find M's corresponding constructor results in an
 *   ambiguity or in a function that is deleted ...".
 * Whether class c's default constructor is implicitly declared or defaulted
 * ([dcl.fct.def.default]) and deleted.  Not analysed (false): a const member
 * of class type, the anonymous-union bullets, access.
 */
bool semantic_analysis::default_constructor_deleted(entity_index c) const
{
    const entity &x = entity_table[c];
    if (x.implicit_default_deleted)
        return true;
    if (x.union_) {
        /* [class.default.ctor]/2.2: a union with no default member
           initializers and a variant member with a non-trivial default
           constructor has a deleted defaulted default constructor */
        bool has_default_init = false;
        bool has_nontrivial = false;
        for (uint32_t i = 0; i < x.member_count; i++) {
            const entity &m = entity_table[class_members[x.first_member + i]];
            if (m.kind != entity_kind::variable || !m.nonstatic_member)
                continue;
            if (m.default_initializer)
                has_default_init = true;
            if (entity_index mc = element_class(m.type))
                if (!trivial_special_members(mc))
                    has_nontrivial = true;
        }
        return !has_default_init && has_nontrivial;
    }
    /* a user-declared constructor and no X() = default: no implicit
       default constructor ([class.default.ctor]/1) */
    if (!x.defaulted_default_constructor)
        for (entity_index k = x.first_constructor; k;
             k = entity_table[k].next_constructor)
            if (!entity_table[k].implicit)
                return false;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (m.kind != entity_kind::variable || !m.nonstatic_member ||
            m.default_initializer || (m.bit_field && !m.name))
            continue;
        qualified_type t = m.type;
        type_kind k = type_table[t.unqualified()].kind;
        if (k == type_kind::lvalue_reference ||
            k == type_kind::rvalue_reference)
            return true;
        while (type_table[t.unqualified()].kind == type_kind::array)
            t = type_table[t.unqualified()].operand;
        if ((cv_of(type_table, m.type) & cv_const) &&
            type_table[t.unqualified()].kind != type_kind::class_type)
            return true;
    }
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    /* the last bullet: a subobject whose destructor is deleted */
    for (entity_index k : classes)
        if (entity_table[k].kind == entity_kind::class_type &&
            destructor_deleted(k))
            return true;
    for (entity_index k : classes)
        if (entity_table[k].kind == entity_kind::class_type &&
            default_constructed(c, k) && default_constructor_deleted(k))
            /* a member with a default member initializer is not
               default-initialized by the constructor ([class.base.init]/9.1),
               and a deleted function it would call does not delete this one */
            return true;
    return false;
}

/*
 * [class.copy.ctor]/6: "If the class definition declares a move
 * constructor or move assignment operator, the implicitly declared copy
 * constructor is defined as deleted"; /10: "A defaulted copy/move
 * constructor for a class X is defined as deleted if X has: a potentially
 * constructed subobject type M (or array thereof) that cannot be copied"
 * (here: whose copy constructor is deleted by /6).  Analysis sees only the
 * defaulted move members (a user-provided one makes the class no less
 * dynamic-free, and its copy is the one written or none).
 */
bool semantic_analysis::copy_constructor_deleted_by_move(entity_index c) const
{
    const entity &x = entity_table[c];
    if ((x.defaulted_move_constructor || x.defaulted_move_assignment) &&
        !x.defaulted_copy_constructor)
        return true;
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes)
        if (copy_constructor_deleted_by_move(k))
            return true;
    return false;
}

/* [class.copy.assign]/3: "A user-declared move assignment operator
   X::operator= is a non-static non-template member function of class X with
   exactly one parameter of type X&&, const X&&, volatile X&&, or const
   volatile X&&"; whether class c declares one (true when the lookup of
   operator= is not followed) */
bool semantic_analysis::declares_move_assignment(entity_index c)
{
    lookup_result found = lookup_qualified(
        c, operator_name(preprocessing_op_or_punc::EQUAL), lookup_filter::any);
    if (found.unknown || found.ambiguous)
        return true;
    for (entity_index o = found.entity; o; o = entity_table[o].next_overload) {
        if (entity_table[o].kind != entity_kind::function)
            return true;
        if (move_assignment_operator(o, c))
            return true;
    }
    return false;
}

/*
 * [class.copy.assign]/2: "If the class definition declares a move
 * constructor or move assignment operator, the implicitly declared copy
 * assignment operator is defined as deleted"; /7: "A defaulted copy/move
 * assignment operator for class X is defined as deleted if X has:
 * - a non-static data member of reference type (7.3), or
 * - a direct non-static data member of class type M (or array thereof) or a
 *   direct base class M that cannot be copied/moved because overload
 *   resolution ... results in ... a function that is deleted" (7.4: here
 *   M's implicit copy assignment deleted by these rules).
 * Whether the implicit copy assignment operator of class c, whose
 * subobjects are assigned bitwise (assigned_bitwise), is deleted; a closure
 * type's is ([expr.prim.lambda.closure]/13).  7.2, a const member, is
 * bytewise_const_member's.
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

/* [class.copy.assign]/7.2: "a non-static data member of const non-class
   type (or array thereof)", here for a class assigned bytewise, in it or a
   subobject's class (a const member of class type is not analysed: true) */
bool semantic_analysis::bytewise_const_member(entity_index c) const
{
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.member_count; i++)
        if (cv_of(type_table,
                  entity_table[class_members[x.first_member + i]].type) &
            cv_const)
            return true;
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes)
        if (entity_table[k].kind == entity_kind::class_type &&
            bytewise_const_member(k))
            return true;
    return false;
}

} // namespace cplusplus
