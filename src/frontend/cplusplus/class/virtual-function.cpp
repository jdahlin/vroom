/*
 * virtual-function.cpp: [class.virtual] virtual functions and [class.abstract]
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
 * virtual destructors in the primary virtual table only (two slots, the
 * complete object and the deleting destructor, [abi:vtable-components]),
 * not pure ones; no virtual operator functions, no virtual functions in
 * unions or local classes.
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
    /* [class.virtual]/7: "Even though destructors are not inherited, a
       destructor in a derived class overrides a base class destructor
       declared virtual" */
    if (gx.special == special_member::destructor ||
        entity_table[e].special == special_member::destructor)
        return gx.special == entity_table[e].special;
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
                                                  SyntaxRef declaration,
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
    if (entity_table[e].operator_function || entity_table[c].union_ ||
        local_class_rule(c))
        /* a virtual operator function's calls through operator expressions,
           [class.union]/2's "A union shall not have virtual functions",
           a local class's virtual table: not analysed (a constexpr
           virtual function is an inline one, [dcl.constexpr]/1) */
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
 * [class.virtual]/2, /9 (a final overrider is unique): slot k of the
 * virtual table bg of virtual base v in class c, taken from c's direct
 * bases' groups for v (other than v itself); v's own function if none
 * overrides it, ambiguous when two bases have different overriders.
 */
entity_index semantic_analysis::virtual_base_overrider(entity_index c,
                                                       entity_index v,
                                                       const vtable_group &bg,
                                                       uint32_t k,
                                                       bool &ambiguous) const
{
    entity_index own = vtable_slots[bg.first_slot + k];
    entity_index found = own;
    uint32_t first = entity_table[c].first_base;
    for (uint32_t i = first; i < first + entity_table[c].base_count; i++) {
        entity_index b = element_class(entity_table[class_bases[i]].type);
        const dynamic_class *bd = dynamic_of(b);
        if (b == v || !bd)
            continue;
        for (uint32_t g = 0; g < bd->group_count; g++) {
            const vtable_group &h = vtable_groups[bd->first_group + g];
            if (h.in_virtual_base != v || h.offset_in_virtual != bg.offset ||
                h.subobject_class != bg.subobject_class)
                continue;
            entity_index f = vtable_slots[h.first_slot + k];
            if (f == own || f == found)
                continue;
            if (found != own)
                ambiguous = true;
            found = f;
        }
    }
    return found;
}

/* [abi:class-types] I: the primary base, "the first non-virtual dynamic
   base class"; false if a base without a virtual table pointer comes
   before it */
bool semantic_analysis::primary_base_choice(entity_index c,
                                            entity_index &primary) const
{
    const entity &x = entity_table[c];
    bool nonvirtual_before = false;
    for (uint32_t i = 0; i < x.base_count && !primary; i++) {
        entity_index specifier = class_bases[x.first_base + i];
        const entity &base = entity_table[specifier];
        if (base.virtual_base)
            continue;
        if (!entity_table[type_table[base.type.unqualified()].entity].dynamic)
            nonvirtual_before = true;
        else if (nonvirtual_before)
            return false;
        else
            primary = specifier;
    }
    return true;
}

bool semantic_analysis::primary_chain_has(entity_index k, entity_index v) const
{
    for (;;) {
        const dynamic_class *d = dynamic_of(k);
        if (!d || !d->primary_base)
            return false;
        entity_index b =
            type_table[entity_table[d->primary_base].type.unqualified()].entity;
        if (d->primary_is_virtual && b == v)
            return true;
        k = b;
    }
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
                                                        SyntaxRef n)
{
    /* a copy: declare_implicit below adds an entity, which may move
       entity_table's (its bases, members and type, read here, stay) */
    const entity x = entity_table[c];
    entity_index primary = 0;
    if (!primary_base_choice(c, primary))
        /* a primary base that is not the first base: allocated first
           ([abi:class-types] II), out of declaration order */
        return not_supported(n, "class.virtual");
    /* [abi:class-types] I: without a non-virtual dynamic base, "a nearly
       empty virtual base class" (one with nothing but its virtual table
       pointer) is the primary base, the first in inheritance graph order,
       sharing the virtual table pointer */
    owned_array<entity_index> virtual_order;
    virtual_bases_in_graph_order(c, virtual_order);
    bool primary_virtual = false;
    if (!primary)
        for (entity_index v : virtual_order)
            if (!primary && entity_table[v].dynamic &&
                entity_table[v].nv_size == pointer_size()) {
                /* its secondary tables are not followed */
                if (dynamic_of(v)->group_count != 1)
                    return not_supported(n, "class.mi");
                primary = virtual_base_of(c, v);
                primary_virtual = true;
            }
    /* the virtual bases allocated with the primary base chain */
    entity_index primary_class = 0;
    if (primary)
        primary_class =
            type_table[entity_table[primary].type.unqualified()].entity;
    auto allocated_with_primary = [&](entity_index v) {
        return primary && ((primary_virtual && primary_class == v) ||
                           primary_chain_has(primary_class, v));
    };
    /* [abi:definitions]: a dynamic class "requires a virtual table pointer
       (because it or its bases have one or more virtual member functions
       or virtual base classes)" */
    bool any = primary != 0 || x.virtual_base_count;
    for (const declared_virtual &v : declared_virtuals)
        if (v.c == c)
            any = true;
    if (!any)
        return analysis_result::well_formed;
    /* [class.dtor]/13: "If a class has a base class with a virtual
       destructor, its destructor (whether user- or implicitly-declared)
       is virtual": the implicit one declared here, before the virtual
       table that has it ([class.dtor]/4) */
    if (!entity_table[c].destructor) {
        bool virtual_destructor = false;
        for (uint32_t i = 0; i < x.base_count; i++) {
            entity_index b =
                type_table[entity_table[class_bases[x.first_base + i]]
                               .type.unqualified()]
                    .entity;
            entity_index bd = entity_table[b].destructor;
            if (bd && entity_table[bd].virtual_)
                virtual_destructor = true;
        }
        if (virtual_destructor) {
            declare_implicit(c, special_member::destructor, false);
            entity_index e = entity_table[c].destructor;
            entity_table[e].virtual_ = true;
            declared_virtual v;
            v.c = c;
            v.f = e;
            v.declaration = n;
            declared_virtuals.append(v);
        }
    }
    /* [class.dtor]/6: an implicitly declared destructor is deleted when a
       subobject's destructor is deleted or inaccessible; its virtual table
       slots point at __cxa_deleted_virtual ([abi:pure-virtual]) */
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes) {
        entity_index kd = entity_table[k].destructor;
        if (!(subobject_destructor_inaccessible(c, k) ||
              (kd && entity_table[kd].deleted)))
            continue;
        entity_index own = entity_table[c].destructor;
        if (own && !entity_table[own].implicit)
            return not_supported_because(
                n, "class.dtor",
                "a subobject's destructor is deleted or inaccessible (the "
                "class's is defined as defaulted)");
        if (own)
            entity_table[own].deleted = true;
        if (own && entity_table[own].virtual_) {
            /* [class.virtual]/17: "A function with a deleted definition
               shall not override a function that does not have a deleted
               definition." */
            const entity &cx = entity_table[c];
            for (uint32_t i = 0; i < cx.base_count + cx.virtual_base_count;
                 i++) {
                entity_index base;
                if (i < cx.base_count)
                    base = class_bases[cx.first_base + i];
                else
                    base = class_virtual_bases[cx.first_virtual_base + i -
                                               cx.base_count];
                entity_index bd =
                    entity_table
                        [type_table[entity_table[base].type.unqualified()]
                             .entity]
                            .destructor;
                if (bd && entity_table[bd].virtual_ &&
                    !entity_table[bd].deleted)
                    return ill_formed(n,
                                      "a function with a deleted "
                                      "definition shall not override a "
                                      "function that does not have a "
                                      "deleted definition",
                                      "class.virtual");
            }
        }
        break;
    }
    entity_index destructor = entity_table[c].destructor;
    if (destructor && entity_table[destructor].virtual_ &&
        entity_table[destructor].pure)
        /* a pure virtual destructor, still defined ([class.dtor]/12): its
           slots and its deleting destructor are not analysed */
        return not_supported(n, "class.dtor");
    dynamic_class d;
    d.primary_base = primary;
    d.primary_is_virtual = primary_virtual;
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
        /* [abi:vtable-components]: "The entries for virtual destructors
           are actually pairs of entries.  The first destructor, called the
           complete object destructor, performs the destruction without
           calling delete() on the object.  The second destructor, called
           the deleting destructor, calls delete() after destroying the
           object." */
        uint32_t entries = 1;
        if (entity_table[v.f].special == special_member::destructor)
            entries = 2;
        for (uint32_t k = 0; k < entries; k++) {
            vtable_slots.append(v.f);
            vtable_adjustments.append(0);
            vtable_own.append(true);
            d.slot_count++;
        }
    }
    vtable_group primary_group;
    primary_group.first_slot = d.first_slot;
    primary_group.slot_count = d.slot_count;
    primary_group.subobject_class = c;
    vtable_groups.append(primary_group);
    vtable_group_sources.append(group_source{});
    /* [abi:vtable-components]: the primary virtual table's vbase offsets,
       the primary base's first ("in classes sharing a virtual table with
       a primary base class, the vcall and vbase offsets added by the
       derived class all come before the vcall and vbase offsets required
       by the base class"), then the class's other virtual bases in
       inheritance graph order */
    d.first_vbase_entry = static_cast<uint32_t>(vbase_entries.size());
    if (pd)
        for (uint32_t k = 0; k < pd->vbase_entry_count; k++)
            vbase_entries.append(vbase_entries[pd->first_vbase_entry + k]);
    for (entity_index v : virtual_order) {
        bool seen = false;
        for (uint32_t k = d.first_vbase_entry; k < vbase_entries.size(); k++)
            seen = seen || vbase_entries[k] == v;
        if (!seen)
            vbase_entries.append(v);
    }
    d.vbase_entry_count =
        static_cast<uint32_t>(vbase_entries.size()) - d.first_vbase_entry;
    /* then the secondary virtual tables: the primary base's own secondary
       ones, then each other dynamic base's group, each slot the class
       overrides replaced (called through a this-adjusting thunk) */
    bool across_virtual = false;
    /* the groups of a base's virtual table group that are not in one of
       its virtual bases (each virtual base's are the class's own, after
       the others) */
    auto secondary = [&](entity_index base, entity_index virtual_class,
                         const dynamic_class *bd, uint32_t from) {
        for (uint32_t g = from; g < bd->group_count; g++) {
            /* a copy: vtable_groups grows below */
            const vtable_group bg = vtable_groups[bd->first_group + g];
            if (bg.in_virtual_base)
                continue;
            vtable_group group;
            group.first_slot = static_cast<uint32_t>(vtable_slots.size());
            group.slot_count = bg.slot_count;
            group.subobject_class = bg.subobject_class;
            group.in_virtual_base = virtual_class;
            group.offset_in_virtual = bg.offset;
            group.virtual_primary = virtual_class && g == 0;
            for (uint32_t k = 0; k < bg.slot_count; k++) {
                copy(bg.first_slot + k);
                entity_index own = vtable_slots.back();
                bool ambiguous = false;
                if (virtual_class) {
                    /* [class.virtual]/2: the final overrider another base
                       of c has for the virtual base's function */
                    vtable_slots.back() = virtual_base_overrider(
                        c, virtual_class, bg, k, ambiguous);
                }
                for (const declared_virtual &v : declared_virtuals)
                    if (v.c == c && overrides_function(v.f, own)) {
                        vtable_slots.back() = v.f;
                        vtable_own.back() = !virtual_class;
                        ambiguous = false;
                    }
                if (vtable_slots.back() != own && virtual_class) {
                    /* [abi:mangling-special-thunks]: an overrider outside
                       the virtual base is called through a virtual thunk
                       (the lowering's, from the slot's vcall offset);
                       none in a table at a nonzero offset in it */
                    vtable_adjustments.back() = 0;
                    if (bg.offset)
                        across_virtual = true;
                }
                if (ambiguous)
                    across_virtual = true;
                /* a virtual destructor in a secondary virtual table:
                   its thunks are D1's and D0's (the lowering's) */
            }
            group_source source;
            source.base = base;
            source.virtual_class = virtual_class;
            source.offset = bg.offset;
            vtable_groups.append(group);
            vtable_group_sources.append(source);
        }
    };
    if (pd)
        secondary(primary, 0, pd, 1);
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index base = class_bases[x.first_base + i];
        if (base == primary || entity_table[base].virtual_base)
            continue;
        const dynamic_class *bd = dynamic_of(
            type_table[entity_table[base].type.unqualified()].entity);
        if (bd)
            secondary(base, 0, bd, 0);
    }
    /* [abi:vtable-construction] category 3: then a virtual table for each
       dynamic virtual base, in inheritance graph order */
    for (entity_index v : virtual_order) {
        const dynamic_class *vd = dynamic_of(v);
        /* the primary virtual base's table is the class's own */
        if (vd && !allocated_with_primary(v))
            secondary(virtual_base_of(c, v), v, vd, 0);
    }
    d.group_count = static_cast<uint32_t>(vtable_groups.size()) - d.first_group;
    if (across_virtual)
        return not_supported(n, "class.mi");
    if (destructor && entity_table[destructor].virtual_) {
        /* [class.dtor]/12: "At the point of definition of a virtual
           destructor (including an implicit definition), the non-array
           deallocation function is determined as if for the expression
           delete this appearing in a non-virtual destructor of the
           destructor's class": the deleting destructor's
           ([abi:mangling-special-ctor-dtor] D0) */
        VTRY(r, usual_deallocation(n, c, false, false, d.deallocation,
                                   d.deallocation_sized, true, false, false,
                                   true));
        if (entity_table[d.deallocation].access ||
            entity_table[d.deallocation].deleted)
            /* [class.dtor]/6: the destructor is deleted when the lookup
               "results in an ambiguity or in a function that is deleted
               or inaccessible from the destructor" (access from the
               class is not followed) */
            return not_supported(n, "class.dtor");
    }
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
 * the adjustment of each slot the class overrides there ([abi:mangling-special-thunks]: a
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
    entity_index k = element_class(t);
    if (!k)
        return false;
    const dynamic_class *d = dynamic_of(k);
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
