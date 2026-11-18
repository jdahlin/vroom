/*
 * class.virtual.cpp: [class.virtual] virtual functions and [class.abstract]
 * abstract classes (doc/PLAN-cxx-sema.md R6).
 *
 * A member function is virtual when declared so or when it overrides a
 * virtual function of a base ([class.virtual]/1, /2); at the class's
 * closing } its virtual table is made ([abi:vtable-components]): the
 * primary base's slots, each replaced by the class's overrider, then the
 * class's new virtual functions in declaration order.  What is analysed:
 * one dynamic base at most, the first (the primary base, at offset 0);
 * overriders with the overridden function's type (no covariant returns);
 * no virtual destructors (D0 needs operator delete, R8), no virtual
 * operator functions, no virtual functions in unions or local classes.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

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
       (any slot of a direct base's virtual table is one, inherited or
       not, under its first declaration's name) */
    uint32_t overrides = ~0u;
    entity_index overridden = 0;
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index b = type_table[entity_table[class_bases[x.first_base + i]]
                                        .type.unqualified()]
                             .entity;
        const dynamic_class *d = dynamic_of(b);
        if (!d)
            continue;
        for (uint32_t k = 0; k < d->slot_count; k++) {
            entity_index g = vtable_slots[d->first_slot + k];
            const entity &gx = entity_table[g];
            if (gx.name != entity_table[e].name ||
                gx.member_cv != entity_table[e].member_cv ||
                !same_parameters(type_table, gx.type.unqualified(),
                                 entity_table[e].type.unqualified()))
                continue;
            if (overridden)
                /* two bases' functions overridden: more than one dynamic
                   base, which dynamic_class_record refuses anyway */
                return not_supported(declaration, "class.virtual");
            overridden = g;
            overrides = k;
        }
    }
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
    uint32_t dynamic_bases = 0;
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index b = type_table[entity_table[class_bases[x.first_base + i]]
                                        .type.unqualified()]
                             .entity;
        if (!entity_table[b].dynamic)
            continue;
        dynamic_bases++;
        if (i)
            /* a primary base that is not the first base: allocated first
               ([abi:class-types] II), out of declaration order */
            return not_supported(n, "class.virtual");
        primary = class_bases[x.first_base + i];
    }
    if (dynamic_bases > 1)
        /* secondary virtual tables and this-adjusting thunks */
        return not_supported(n, "class.virtual");
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
    if (primary) {
        entity_index b =
            type_table[entity_table[primary].type.unqualified()].entity;
        const dynamic_class &bd = dynamic_classes[entity_table[b].dynamic - 1];
        for (uint32_t k = 0; k < bd.slot_count; k++)
            vtable_slots.append(vtable_slots[bd.first_slot + k]);
        d.slot_count = bd.slot_count;
    }
    for (const declared_virtual &v : declared_virtuals) {
        if (v.c != c)
            continue;
        if (v.overrides != ~0u) {
            vtable_slots[d.first_slot + v.overrides] = v.f;
            entity_table[v.f].vtable_slot = v.overrides;
            continue;
        }
        entity_table[v.f].vtable_slot = d.slot_count;
        vtable_slots.append(v.f);
        d.slot_count++;
    }
    /* the key function is among all the class's virtual functions,
       overriders too, in declaration order */
    for (const declared_virtual &v : declared_virtuals)
        if (v.c == c && !d.key_function && !entity_table[v.f].pure &&
            !entity_table[v.f].inline_)
            d.key_function = v.f;
    for (uint32_t k = 0; k < d.slot_count; k++)
        if (entity_table[vtable_slots[d.first_slot + k]].pure)
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
