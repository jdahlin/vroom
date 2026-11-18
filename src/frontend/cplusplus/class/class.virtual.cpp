/*
 * class.virtual.cpp: [class.virtual] virtual functions and [class.abstract]
 * abstract classes (doc/PLAN-cxx-sema.md R6).
 *
 * A member function is virtual when declared so or when it overrides a
 * virtual function of a base ([class.virtual]/1, /2); at the class's
 * closing } its virtual table is made ([abi:vtable-components]): the
 * primary base's slots, each replaced by the class's overrider, then the
 * class's new virtual functions in declaration order, then a secondary
 * table per other dynamic base ([abi:vtable-general]), whose slots the
 * class overrides call through non-virtual thunks.  What is analysed: no
 * virtual bases ([class.mi]), the primary base the first base;
 * overriders with the overridden function's type (no covariant returns);
 * no virtual destructors (D0 needs operator delete, R8), no virtual
 * operator functions, no virtual functions in unions or local classes.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* [class.virtual]/2: whether member function e has the name,
   parameter-type-list and cv-qualification of virtual function g */
bool semantic_analysis::overrides_function(entity_index e, entity_index g) const
{
    const entity &gx = entity_table[g];
    return gx.name == entity_table[e].name &&
           gx.member_cv == entity_table[e].member_cv &&
           same_parameters(type_table, gx.type.unqualified(),
                           entity_table[e].type.unqualified());
}

/*
 * A non-static member function e just declared in class c: whether it is
 * virtual ([class.virtual]/1: "if it is first declared with the keyword
 * virtual or if it overrides a virtual member function declared in a base
 * class"), with the virt-specifiers and pure-specifier its
 * member-declarator had (virt_override, virt_final, virt_pure).
 */
analysis_result semantic_analysis::virtual_member(entity_index c,
                                                  entity_index e,
                                                  node_index declaration,
                                                  token_index id,
                                                  bool virtual_specified)
{
    bool specified =
        virtual_specified || virt_override || virt_final || virt_pure;
    /* [class.virtual]/2: overriding: the same name, parameter-type-list,
       cv-qualification and ref-qualifier as a virtual function of a base
       (any slot of a direct base's virtual table group is one, inherited
       or not, under its first declaration's name); each function
       overridden is checked below, in every base it is in */
    entity_index overridden = 0;
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index b = type_table[entity_table[class_bases[x.first_base + i]]
                                        .type.unqualified()]
                             .entity;
        const dynamic_class *d = dynamic_of(b);
        if (!d)
            continue;
        const vtable_group &first = vtable_groups[d->first_group];
        const vtable_group &last =
            vtable_groups[d->first_group + d->group_count - 1];
        for (uint32_t k = first.first_slot;
             k < last.first_slot + last.slot_count; k++) {
            entity_index g = vtable_slots[k];
            if (!overrides_function(e, g))
                continue;
            if (overridden && overridden != g &&
                entity_table[g].type != entity_table[overridden].type)
                return not_supported(declaration, "class.virtual");
            if (entity_table[g].final_function)
                overridden = g;
            if (!overridden)
                overridden = g;
        }
    }
    uint32_t overrides = ~0u;
    if (!overridden && !specified)
        return analysis_result::well_formed;
    if (entity_table[e].operator_function || entity_table[e].constexpr_ ||
        entity_table[c].union_ || local_class_rule(c))
        /* a virtual operator function's calls through operator expressions,
           [class.union]/2's "A union shall not have virtual functions",
           a local class's virtual table: not analysed */
        return not_supported(declaration, "class.virtual");
    if (!entity_table[e].nonstatic_member) {
        /* [class.static.mfct]/2: "A static member function shall not be
           virtual" */
        if (virtual_specified)
            return ill_formed_at_token(id,
                                       "a static member function shall not "
                                       "be virtual",
                                       "class.static.mfct");
        return not_supported(declaration, "class.virtual");
    }
    if (overridden) {
        const entity &g = entity_table[overridden];
        /* [class.virtual]/8: "The return type of an overriding function
           shall be either identical to the return type of the overridden
           function or covariant": covariant returns are not analysed */
        if (g.type != entity_table[e].type)
            return not_supported(declaration, "class.virtual");
        /* [class.virtual]/4: "If a virtual function f in some class B is
           marked with the virt-specifier final and in a class D derived
           from B a function D::f overrides B::f, the program is
           ill-formed." */
        if (g.final_function)
            return ill_formed_at_token(id,
                                       "a function shall not override a "
                                       "virtual function marked final",
                                       "class.virtual");
        if (g.deleted || entity_table[e].deleted)
            /* [class.virtual]/17 */
            return not_supported(declaration, "class.virtual");
    } else if (virt_override) {
        /* [class.virtual]/5: "If a virtual function is marked with the
           virt-specifier override and does not override a member function
           of a base class, the program is ill-formed." */
        return ill_formed_at_token(id,
                                   "a virtual function marked override "
                                   "shall override a member function of a "
                                   "base class",
                                   "class.virtual");
    } else if (!virtual_specified) {
        /* [class.mem]/14: "A virt-specifier-seq shall appear only in the
           first declaration of a virtual member function"; /12: "A
           pure-specifier shall be used only in the declaration of a
           virtual function" */
        return ill_formed_at_token(id,
                                   "a virt-specifier-seq or pure-specifier "
                                   "shall be used only in the declaration "
                                   "of a virtual function",
                                   "class.mem");
    }
    if (entity_table[e].deleted)
        return not_supported(declaration, "class.virtual");
    entity_table[e].virtual_ = true;
    entity_table[e].pure = virt_pure;
    entity_table[e].final_function = virt_final;
    declared_virtual v;
    v.c = c;
    v.f = e;
    v.overrides = overrides;
    v.declaration = declaration;
    declared_virtuals.append(v);
    return analysis_result::well_formed;
}

/*
 * At the closing } of class c (before its layout): [class.virtual]/1 "A
 * class that declares or inherits a virtual function is called a
 * polymorphic class"; its virtual table ([abi:vtable-components]) is its
 * primary base's ([abi:class-types] I: "If C has a dynamic base class,
 * attempt to choose a primary base class B" -- the first non-virtual
 * dynamic base) with the slots the class overrides replaced, then a slot
 * per new virtual function in declaration order.  [class.abstract]/4: "A
 * class is abstract if it contains or inherits at least one pure virtual
 * function for which the final overrider is pure virtual."
 * [abi:vague-vtable]: the key function is "the first non-pure virtual
 * function that is not inline at the point of class definition".
 */
analysis_result semantic_analysis::dynamic_class_record(entity_index c,
                                                        node_index n)
{
    const entity &x = entity_table[c];
    entity_index primary = 0;
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index b = type_table[entity_table[class_bases[x.first_base + i]]
                                        .type.unqualified()]
                             .entity;
        if (!entity_table[b].dynamic)
            continue;
        if (i && !primary)
            /* a primary base that is not the first base: allocated first
               ([abi:class-types] II), out of declaration order */
            return not_supported(n, "class.virtual");
        if (!primary)
            primary = class_bases[x.first_base + i];
    }
    bool any = primary != 0;
    for (const declared_virtual &v : declared_virtuals)
        if (v.c == c)
            any = true;
    if (!any)
        return analysis_result::well_formed;
    if (entity_table[c].destructor)
        /* [class.cdtor]: a destructor of a dynamic class, whose base's may
           be virtual (D0 needs the deallocation function, R8) */
        return not_supported(n, "class.virtual");
    dynamic_class d;
    d.primary_base = primary;
    d.first_slot = static_cast<uint32_t>(vtable_slots.size());
    d.first_group = static_cast<uint32_t>(vtable_groups.size());
    /* a slot copied from a base's group, with its adjustment */
    auto copy = [&](uint32_t from) {
        vtable_slots.append(vtable_slots[from]);
        vtable_adjustments.append(vtable_adjustments[from]);
        vtable_own.append(false);
    };
    /* [abi:vtable-components]: the primary virtual table: the primary
       base's, then a slot per virtual function the class declares that
       overrides none of the primary base's functions */
    const dynamic_class *pd = nullptr;
    if (primary) {
        entity_index b =
            type_table[entity_table[primary].type.unqualified()].entity;
        pd = dynamic_of(b);
        for (uint32_t k = 0; k < pd->slot_count; k++)
            copy(pd->first_slot + k);
        d.slot_count = pd->slot_count;
    }
    for (const declared_virtual &v : declared_virtuals) {
        if (v.c != c)
            continue;
        bool replaced = false;
        for (uint32_t k = 0; k < d.slot_count; k++) {
            uint32_t s = d.first_slot + k;
            if (!overrides_function(v.f, vtable_slots[s]))
                continue;
            vtable_slots[s] = v.f;
            vtable_adjustments[s] = 0;
            vtable_own[s] = true;
            if (!replaced)
                entity_table[v.f].vtable_slot = k;
            replaced = true;
        }
        if (replaced)
            continue;
        entity_table[v.f].vtable_slot = d.slot_count;
        vtable_slots.append(v.f);
        vtable_adjustments.append(0);
        vtable_own.append(true);
        d.slot_count++;
    }
    vtable_group primary_group;
    primary_group.first_slot = d.first_slot;
    primary_group.slot_count = d.slot_count;
    vtable_groups.append(primary_group);
    vtable_group_sources.append(group_source{});
    /* then the secondary virtual tables: the primary base's own secondary
       ones, then each other dynamic base's group, each slot the class
       overrides replaced (called through a this-adjusting thunk) */
    auto secondary = [&](entity_index base, const dynamic_class *bd,
                         uint32_t from) {
        for (uint32_t g = from; g < bd->group_count; g++) {
            const vtable_group &bg = vtable_groups[bd->first_group + g];
            vtable_group group;
            group.first_slot = static_cast<uint32_t>(vtable_slots.size());
            group.slot_count = bg.slot_count;
            for (uint32_t k = 0; k < bg.slot_count; k++) {
                copy(bg.first_slot + k);
                for (const declared_virtual &v : declared_virtuals)
                    if (v.c == c &&
                        overrides_function(v.f, vtable_slots.back())) {
                        vtable_slots.back() = v.f;
                        vtable_own.back() = true;
                    }
            }
            vtable_groups.append(group);
            group_source source;
            source.base = base;
            source.offset = bg.offset;
            vtable_group_sources.append(source);
        }
    };
    if (pd)
        secondary(primary, pd, 1);
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index base = class_bases[x.first_base + i];
        if (base == primary)
            continue;
        const dynamic_class *bd = dynamic_of(
            type_table[entity_table[base].type.unqualified()].entity);
        if (bd)
            secondary(base, bd, 0);
    }
    d.group_count = static_cast<uint32_t>(vtable_groups.size()) - d.first_group;
    /* the key function is among all the class's virtual functions,
       overriders too, in declaration order */
    for (const declared_virtual &v : declared_virtuals)
        if (v.c == c && !d.key_function && !entity_table[v.f].pure &&
            !entity_table[v.f].inline_)
            d.key_function = v.f;
    for (uint32_t k = d.first_slot; k < vtable_slots.size(); k++)
        if (entity_table[vtable_slots[k]].pure)
            d.abstract = true;
    if (d.abstract) {
        /* a member declared while the class was incomplete whose type
           has the class by value (a static data member, a member
           function's parameter or return type): [basic.def]/5 and
           [class.abstract]/3, which clang applies at the closing } */
        type_index self = x.type.unqualified();
        for (entity_index e = c + 1; e < entity_table.size(); e++) {
            const entity &m = entity_table[e];
            if (m.enclosing != c)
                continue;
            const struct type &t = type_table[m.type.unqualified()];
            if (m.kind == entity_kind::variable && !m.nonstatic_member) {
                qualified_type u = m.type;
                while (type_table[u.unqualified()].kind == type_kind::array)
                    u = type_table[u.unqualified()].operand;
                if (u.unqualified() == self)
                    return not_supported(n, "class.abstract");
                continue;
            }
            if (m.kind != entity_kind::function ||
                t.kind != type_kind::function)
                continue;
            if (t.operand.unqualified() == self)
                return not_supported(n, "class.abstract");
            const qualified_type *ps =
                type_table.parameters(m.type.unqualified());
            for (uint32_t i = 0; i < t.parameter_count; i++)
                if (ps[i].unqualified() == self)
                    return not_supported(n, "class.abstract");
        }
    }
    dynamic_classes.append(d);
    entity_table[c].dynamic = static_cast<uint32_t>(dynamic_classes.size());
    return analysis_result::well_formed;
}

/*
 * After class c's layout: each secondary virtual table's subobject offset
 * (its base subobject's offset plus the table's offset in that base) and
 * the adjustment of each slot the class overrides there ([abi:thunks]: a
 * non-virtual thunk subtracts the subobject's offset from this).
 */
void semantic_analysis::dynamic_class_offsets(entity_index c)
{
    const dynamic_class *d = dynamic_of(c);
    if (!d)
        return;
    for (uint32_t g = d->first_group; g < d->first_group + d->group_count;
         g++) {
        vtable_group &group = vtable_groups[g];
        const group_source &source = vtable_group_sources[g];
        if (source.base)
            group.offset = entity_table[source.base].offset + source.offset;
        for (uint32_t k = 0; k < group.slot_count; k++)
            if (vtable_own[group.first_slot + k])
                vtable_adjustments[group.first_slot + k] =
                    -static_cast<int64_t>(group.offset);
    }
}

/* [class.abstract]/2: whether t is (an array of) an abstract class */
bool semantic_analysis::abstract_class(qualified_type t) const
{
    while (type_table[t.unqualified()].kind == type_kind::array)
        t = type_table[t.unqualified()].operand;
    const struct type &r = type_table[t.unqualified()];
    if (r.kind != type_kind::class_type)
        return false;
    const dynamic_class *d = dynamic_of(r.entity);
    return d && d->abstract;
}

/*
 * [class.cdtor]/4: "When a virtual function is called directly or
 * indirectly from a constructor or from a destructor ... the function
 * called is the final overrider in the constructor's or destructor's
 * class and not one overriding it in a more-derived class": [abi:vtable-ctor]
 * the constructor stores its class's virtual table pointer after the
 * bases are constructed, before the members.  The statement that does,
 * 0 when class c is not dynamic.
 */
uint32_t semantic_analysis::vtable_pointer_store(entity_index c, token_index at)
{
    if (!entity_table[c].dynamic)
        return 0;
    expression_meaning self;
    if (this_pointer(at, self) != analysis_result::well_formed)
        return 0;
    expression_meaning stored;
    stored.type = qualified_type::make(
        type_table.fundamental(fundamental_type::void_), cv_none);
    stored.category = value_category::prvalue;
    stored.evaluation = constness::not_constant;
    uint32_t e = expression_node(semantic_kind::vtable_pointer_store, stored,
                                 at, self.node, c);
    return statement_node(semantic_kind::expression_statement, at, e);
}

} // namespace cplusplus
