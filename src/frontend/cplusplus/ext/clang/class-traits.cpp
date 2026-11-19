/*
 * class-traits.cpp: [ext:clang_type_traits] the traits of one type that ask
 * about a class's declaration ([meta.unary.prop]: is_abstract,
 * is_polymorphic, is_aggregate, is_empty, is_trivial, is_trivially_copyable,
 * is_trivially_destructible, has_virtual_destructor), from what the
 * analysis keeps of the class: its members and bases, its virtual
 * functions, its special members.  Those not decided here (is_final,
 * is_standard_layout, is_pod, has_unique_object_representations) are
 * unsupported.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* [class.prop]/2 (an empty class): "no non-static data members other than
   bit-fields of size zero, no virtual functions, no virtual base classes,
   and no non-empty base classes" ([meta.unary.prop]: and not a union) */
bool semantic_analysis::empty_class(entity_index c) const
{
    const entity &x = entity_table[c];
    if (x.union_ || x.dynamic || x.virtual_base_count)
        return false;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (m.base_subobject || m.kind != entity_kind::variable ||
            !m.nonstatic_member)
            continue;
        if (!m.bit_field || m.bit_width)
            return false;
    }
    for (uint32_t i = 0; i < x.base_count; i++)
        if (!empty_class(type_table[entity_table[class_bases[x.first_base + i]]
                                        .type.unqualified()]
                             .entity))
            return false;
    return true;
}

/* [class.virtual]/1 (polymorphic): a class that declares or inherits a
   virtual function (a virtual base alone does not make one) */
bool semantic_analysis::polymorphic_class(entity_index c) const
{
    const entity &x = entity_table[c];
    if (const dynamic_class *d = dynamic_of(c))
        if (d->slot_count)
            return true;
    for (uint32_t i = 0; i < x.base_count; i++)
        if (polymorphic_class(
                type_table[entity_table[class_bases[x.first_base + i]]
                               .type.unqualified()]
                    .entity))
            return true;
    for (uint32_t i = 0; i < x.virtual_base_count; i++)
        if (polymorphic_class(
                type_table
                    [entity_table[class_virtual_bases[x.first_virtual_base + i]]
                         .type.unqualified()]
                        .entity))
            return true;
    return false;
}

/* whether a destructor is declared virtual in class c or in a base class
   ([class.dtor]/9: a destructor overriding a virtual one is virtual) */
bool semantic_analysis::virtual_destructor(entity_index c) const
{
    const entity &x = entity_table[c];
    if (x.destructor && entity_table[x.destructor].virtual_)
        return true;
    for (uint32_t i = 0; i < x.base_count; i++)
        if (virtual_destructor(
                type_table[entity_table[class_bases[x.first_base + i]]
                               .type.unqualified()]
                    .entity))
            return true;
    for (uint32_t i = 0; i < x.virtual_base_count; i++)
        if (virtual_destructor(
                type_table
                    [entity_table[class_virtual_bases[x.first_virtual_base + i]]
                         .type.unqualified()]
                        .entity))
            return true;
    return false;
}

/* whether class c, or a subobject's class, has what the analysis does not
   follow to its end in [class.prop]'s triviality: a constructor template or
   a deleted constructor or destructor (which clang counts as it does not
   the standard: "cwg1496: no"), a defaulted move */
bool semantic_analysis::triviality_unknown(entity_index c) const
{
    const entity &x = entity_table[c];
    if (x.constructor_template || x.constructor_templates ||
        x.implicit_copy_deleted || x.defaulted_move_constructor ||
        x.defaulted_move_assignment || x.defaulted_copy_assignment)
        return true;
    if (x.destructor && entity_table[x.destructor].deleted)
        return true;
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor)
        if (entity_table[k].deleted)
            return true;
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes)
        if (triviality_unknown(k))
            return true;
    return false;
}

/* [class.prop]/1: whether class c is trivially copyable; known is false
   when the analysis cannot tell */
bool semantic_analysis::class_trivially_copyable(entity_index c, bool &known)
{
    known = true;
    if (triviality_unknown(c)) {
        known = false;
        return false;
    }
    /* a virtual function or a user-provided destructor: not trivial */
    if (entity_table[c].dynamic || destroys(c))
        return false;
    if (copies_bytes(c) && assigned_bitwise(c))
        return true;
    known = false;
    return false;
}

/* the traits; known is false when the analysis cannot tell */
bool semantic_analysis::class_property_trait(int which, qualified_type t,
                                             bool &answer)
{
    t = qualified_type::make(t.unqualified(), cv_none);
    /* an array of T is as T for what follows ([basic.types]/9) */
    type_kind kind = type_table[t.unqualified()].kind;
    bool array = kind == type_kind::array;
    bool unbounded = array && !type_table[t.unqualified()].bound_known;
    while (kind == type_kind::array) {
        t = type_table[t.unqualified()].operand;
        t = qualified_type::make(t.unqualified(), cv_none);
        kind = type_table[t.unqualified()].kind;
    }
    bool is_class = kind == type_kind::class_type;
    entity_index c = 0;
    if (is_class) {
        c = type_table[t.unqualified()].entity;
        if (!entity_table[c].size)
            return false;
    }
    answer = false;
    switch (static_cast<unary_trait_kind>(which)) {
    case unary_trait_kind::is_abstract:
        answer = !array && is_class && abstract_class(t);
        return true;
    case unary_trait_kind::is_polymorphic:
        answer = !array && is_class && polymorphic_class(c);
        return true;
    case unary_trait_kind::is_aggregate:
        /* is_aggregate does not take a base class that is empty yet
           ([dcl.init.aggr]/1.4's public bases) */
        if (is_class && entity_table[c].base_count)
            return false;
        answer = array || (is_class && is_aggregate(c));
        return true;
    case unary_trait_kind::is_empty:
        answer = !array && is_class && empty_class(c);
        return true;
    case unary_trait_kind::has_virtual_destructor:
        answer = !array && is_class && virtual_destructor(c);
        return true;
    case unary_trait_kind::is_trivial:
        /* [class.prop]/3: trivially copyable with a trivial default
           constructor; a scalar is trivial ([basic.types]/9) */
        if (!is_class) {
            answer = kind != type_kind::function &&
                     kind != type_kind::lvalue_reference &&
                     kind != type_kind::rvalue_reference &&
                     fundamental_of(t) != fundamental_type::void_;
            return true;
        }
        {
            bool known = true;
            bool copyable = class_trivially_copyable(c, known);
            if (!known)
                return false;
            answer = copyable && trivial_special_members(c);
        }
        return true;
    case unary_trait_kind::is_trivially_copyable:
        if (!is_class) {
            answer = kind != type_kind::function &&
                     kind != type_kind::lvalue_reference &&
                     kind != type_kind::rvalue_reference &&
                     fundamental_of(t) != fundamental_type::void_;
            return true;
        }
        {
            bool known = true;
            answer = class_trivially_copyable(c, known);
            if (!known)
                return false;
        }
        return true;
    case unary_trait_kind::is_trivially_destructible:
    case unary_trait_kind::has_trivial_destructor:
        if (kind == type_kind::function ||
            fundamental_of(t) == fundamental_type::void_)
            return true;
        /* is_destructible: an array of unknown bound is not */
        if (unbounded && static_cast<unary_trait_kind>(which) ==
                             unary_trait_kind::is_trivially_destructible)
            return true;
        if (!is_class) {
            answer = true;
            return true;
        }
        {
            bool throws = false;
            answer = class_destructible(c, throws) && !destroys(c);
        }
        return true;
    default:
        return false;
    }
}

} // namespace cplusplus
