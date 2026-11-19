/*
 * temp.func.order.cpp: [temp.func.order] Partial ordering of function
 * templates, with [temp.deduct.partial] Deducing template arguments during
 * partial ordering and [temp.class.order] Partial ordering of class template
 * specializations.
 *
 * [temp.func.order]/3: "To produce the transformed template, for each type,
 * non-type, or template template parameter (including template parameter
 * packs ([temp.variadic]) thereof) synthesize a unique type, value, or class
 * template respectively and substitute it for each occurrence of that
 * parameter in the function type of the template."  A template's own
 * template_parameter types are such unique types to any other template:
 * deduce_type deduces the other's parameters from them and matches them
 * with nothing else.  Templates with non-type parameters, and types
 * deduce_type has no rules for (a template-id, a dependent name, a pointer
 * to member), are not ordered (unknown).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

/* a type partial ordering compares with deduce_type's rules alone */
bool orderable(const types &table, qualified_type t, bool packs)
{
    const struct type &r = table[t.unqualified()];
    switch (r.kind) {
    case type_kind::template_parameter:
    case type_kind::fundamental:
    case type_kind::enumeration:
    case type_kind::class_type:
        return true;
    case type_kind::pack_expansion:
        return packs && orderable(table, r.operand, packs);
    case type_kind::pointer:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
        return orderable(table, r.operand, packs);
    case type_kind::array:
        return r.bound_known && orderable(table, r.operand, packs);
    case type_kind::function: {
        if (!orderable(table, r.operand, packs))
            return false;
        const qualified_type *list = table.parameters(t.unqualified());
        for (uint32_t i = 0; i < r.parameter_count; i++)
            if (!orderable(table, list[i], packs))
                return false;
        return true;
    }
    default:
        return false;
    }
}

bool is_reference(const types &table, qualified_type t)
{
    type_kind k = table[t.unqualified()].kind;
    return k == type_kind::lvalue_reference || k == type_kind::rvalue_reference;
}

/* [temp.deduct.partial]/5: "If P is a reference type, P is replaced by the
   type referred to"; /7: "Remove any top-level cv-qualifiers" (the cv the
   referred type had kept for /6) */
qualified_type transformed(const types &table, qualified_type t,
                           unsigned &referred_cv)
{
    if (is_reference(table, t))
        t = table[t.unqualified()].operand;
    referred_cv = t.cv();
    return qualified_type::make(t.unqualified(), cv_none);
}

/* (a more cv-qualified than b) */
bool more_cv(unsigned a, unsigned b)
{
    return (a & b) == b && a != b;
}

} // namespace

/*
 * [temp.deduct.partial]/10: "Function template F is at least as
 * specialized as function template G if, for each pair of types used to
 * determine the ordering, the type from F is at least as specialized as
 * the type from G."  /3.1: "In the context of a function call, the types
 * used are those function parameter types for which the function call has
 * arguments": per argument, the parameter's type or the pattern of the
 * function parameter pack it falls in.  G's parameters are deduced from
 * F's transformed types (/2), together, so that one parameter of G has one
 * value.
 */
bool semantic_analysis::at_least_as_specialized(uint32_t f, uint32_t g,
                                                size_t arguments, bool &unknown)
{
    const function_template ff = function_templates[f - 1];
    const function_template gg = function_templates[g - 1];
    qualified_type ftype = entity_table[ff.entity].type;
    qualified_type gtype = entity_table[gg.entity].type;
    const struct type fr = type_table[ftype.unqualified()];
    const struct type gr = type_table[gtype.unqualified()];
    const qualified_type *fp = type_table.parameters(ftype.unqualified());
    const qualified_type *gp = type_table.parameters(gtype.unqualified());
    uint32_t ffixed = fr.parameter_count, gfixed = gr.parameter_count;
    if (ffixed && type_table[fp[ffixed - 1].unqualified()].kind ==
                      type_kind::pack_expansion)
        ffixed--;
    if (gfixed && type_table[gp[gfixed - 1].unqualified()].kind ==
                      type_kind::pack_expansion)
        gfixed--;
    if (arguments < ffixed || arguments < gfixed ||
        (ffixed == fr.parameter_count && arguments != ffixed) ||
        (gfixed == gr.parameter_count && arguments != gfixed)) {
        unknown = true;
        return false;
    }
    /* the template parameter pack G's function parameter pack expands
       (its last template parameter) */
    uint32_t pack = gg.parameter_count;
    if (template_parameter_is_pack(gg.first_parameter + gg.parameter_count - 1))
        pack = gg.parameter_count - 1;
    owned_array<template_argument> values;
    owned_array<bool> set;
    for (uint32_t i = 0; i < gg.parameter_count; i++) {
        values.append(template_argument());
        set.append(false);
    }
    bool deduced = true;
    bool penalty = false;
    for (size_t k = 0; k < arguments; k++) {
        bool apack = k >= ffixed, ppack = k >= gfixed;
        qualified_type a = fp[k], p = gp[k];
        if (apack)
            a = type_table[fp[ffixed].unqualified()].operand;
        if (ppack)
            p = type_table[gp[gfixed].unqualified()].operand;
        /* [temp.deduct.type]/10: "During partial ordering
           ([temp.deduct.partial]), if Ai was originally a function
           parameter pack: ... if Pi is not a function parameter pack,
           template argument deduction fails." */
        if (apack && !ppack)
            return false;
        bool references =
            is_reference(type_table, a) && is_reference(type_table, p);
        bool a_lvalue =
            type_table[a.unqualified()].kind == type_kind::lvalue_reference;
        bool p_lvalue =
            type_table[p.unqualified()].kind == type_kind::lvalue_reference;
        unsigned acv = 0, pcv = 0;
        qualified_type at = transformed(type_table, a, acv);
        qualified_type pt = transformed(type_table, p, pcv);
        if (references) {
            /* [temp.deduct.partial]/9: "If, for a given type, the types
               are identical after the transformations above and both P and
               A were reference types (before being replaced with the type
               referred to above): if the type from the argument template
               was an lvalue reference and the type from the parameter
               template was not, the parameter type is not considered to be
               at least as specialized as the argument type; otherwise, if
               the type from the argument template is more cv-qualified
               than the type from the parameter template (as described
               above), the parameter type is not considered to be at least
               as specialized as the argument type."  For F's type to be
               at least as specialized as G's, G's is the argument here */
            owned_array<template_argument> v1, v2;
            owned_array<bool> s1, s2;
            for (uint32_t i = 0; i < gg.parameter_count; i++) {
                v1.append(template_argument());
                s1.append(false);
            }
            for (uint32_t i = 0; i < ff.parameter_count; i++) {
                v2.append(template_argument());
                s2.append(false);
            }
            if (deduce_type(pt, at, v1, s1, true) &&
                deduce_type(at, pt, v2, s2, true)) {
                if (p_lvalue && !a_lvalue)
                    penalty = true;
                else if (more_cv(pcv, acv))
                    penalty = true;
            }
        }
        if (!ppack) {
            if (!deduce_type(pt, at, values, set, true))
                deduced = false;
            continue;
        }
        /* /8: "If P is a function parameter pack, the type A of each
           remaining parameter type of the argument template is compared
           with the type P of the declarator-id of the function parameter
           pack.  Each comparison deduces template arguments for subsequent
           positions in the template parameter packs expanded by the
           function parameter pack." */
        owned_array<template_argument> one;
        owned_array<bool> one_set;
        for (uint32_t i = 0; i < values.size(); i++) {
            one.append(values[i]);
            one_set.append(set[i] && i != pack);
        }
        if (!deduce_type(pt, at, one, one_set, true)) {
            deduced = false;
            continue;
        }
        for (uint32_t i = 0; i < values.size(); i++)
            if (i != pack && one_set[i] && !set[i]) {
                values[i] = one[i];
                set[i] = true;
            }
    }
    return deduced && !penalty;
}

int semantic_analysis::function_template_order(uint32_t f, uint32_t g,
                                               size_t arguments, bool &unknown)
{
    unknown = false;
    const uint32_t both[2] = {f, g};
    for (uint32_t x : both) {
        const function_template t = function_templates[x - 1];
        for (uint32_t i = 0; i < t.parameter_count; i++) {
            /* (a type-parameter, or an invented one, [dcl.fct]/22) */
            template_parameter_parts form;
            if (!template_parameter_form(
                    template_parameter_nodes[t.first_parameter + i], form) ||
                !form.type || form.template_template)
                unknown = true;
        }
        qualified_type type = entity_table[t.entity].type;
        const struct type r = type_table[type.unqualified()];
        const qualified_type *list = type_table.parameters(type.unqualified());
        if (r.kind != type_kind::function || r.variadic)
            unknown = true;
        for (uint32_t i = 0; i < r.parameter_count && !unknown; i++)
            if (!orderable(type_table, list[i], true))
                unknown = true;
    }
    if (unknown)
        return 0;
    bool fg = at_least_as_specialized(f, g, arguments, unknown);
    bool gf = at_least_as_specialized(g, f, arguments, unknown);
    if (unknown)
        return 0;
    if (fg && !gf)
        return 1;
    if (gf && !fg)
        return -1;
    if (!fg)
        return 0;
    /* [temp.deduct.partial]/11: "If, after considering the above, function
       template F is at least as specialized as function template G and
       vice-versa, and if G has a trailing function parameter pack for which
       F does not have a corresponding parameter, and if F does not have a
       trailing function parameter pack, then F is more specialized than
       G." */
    auto trailing_pack = [&](uint32_t x, uint32_t &position) {
        qualified_type type =
            entity_table[function_templates[x - 1].entity].type;
        const struct type r = type_table[type.unqualified()];
        const qualified_type *list = type_table.parameters(type.unqualified());
        position = r.parameter_count;
        if (!r.parameter_count ||
            type_table[list[r.parameter_count - 1].unqualified()].kind !=
                type_kind::pack_expansion)
            return false;
        position = r.parameter_count - 1;
        return true;
    };
    uint32_t fcount = 0, gcount = 0;
    bool fpack = trailing_pack(f, fcount), gpack = trailing_pack(g, gcount);
    if (gpack && !fpack && fcount <= gcount)
        return 1;
    if (fpack && !gpack && gcount <= fcount)
        return -1;
    /* [temp.func.order]/6.2.2: else, with equivalent template-parameter-lists
       and function parameters, "if one is more constrained than the other
       ([temp.constr.order]), the more constrained template is more
       specialized": here a constrained template against one without
       constraints ([temp.constr.order]/1.2's subsumption of two sets of
       constraints is not analysed) */
    if (fcount != gcount || fpack != gpack ||
        function_templates[f - 1].parameter_count !=
            function_templates[g - 1].parameter_count)
        return 0;
    bool fc = template_head_constrained(function_templates[f - 1].declaration);
    bool gc = template_head_constrained(function_templates[g - 1].declaration);
    return static_cast<int>(fc) - static_cast<int>(gc);
}

/*
 * [temp.class.order]/1: "For two class template partial specializations,
 * the first is more specialized than the second if, given the following
 * rewrite to two function templates, the first function template is more
 * specialized than the second according to the ordering rules for function
 * templates": each a function of one parameter, the class template's
 * specialization for the partial specialization's arguments.  Deducing
 * one such parameter from the other ([temp.deduct.type]/9) is deducing
 * t's arguments from s's.
 */
bool semantic_analysis::class_at_least_as_specialized(uint32_t s, uint32_t t,
                                                      bool &unknown)
{
    const function_template ss = function_templates[s - 1];
    const function_template tt = function_templates[t - 1];
    if (ss.pattern_count != tt.pattern_count) {
        unknown = true;
        return false;
    }
    const uint32_t firsts[2] = {ss.first_pattern, tt.first_pattern};
    for (uint32_t i = 0; i < ss.pattern_count; i++)
        for (uint32_t first : firsts) {
            const template_argument a = template_arguments[first + i];
            if (a.kind == template_argument::kind_t::pack ||
                a.kind == template_argument::kind_t::template_name ||
                (a.kind == template_argument::kind_t::type &&
                 !orderable(type_table, a.type, false)))
                unknown = true;
        }
    if (unknown)
        return false;
    owned_array<template_argument> d;
    owned_array<bool> set;
    for (uint32_t i = 0; i < tt.parameter_count; i++) {
        d.append(template_argument());
        set.append(false);
    }
    for (uint32_t i = 0; i < ss.pattern_count; i++) {
        const template_argument x = template_arguments[tt.first_pattern + i];
        const template_argument y = template_arguments[ss.first_pattern + i];
        if (x.kind != y.kind)
            return false;
        if (x.kind == template_argument::kind_t::type) {
            if (!deduce_type(x.type, y.type, d, set, true))
                return false;
            continue;
        }
        if (!x.template_entity) {
            if (!(x == y))
                return false;
            continue;
        }
        if (x.value >= d.size()) {
            unknown = true;
            return false;
        }
        if (set[x.value]) {
            if (!(d[x.value] == y))
                return false;
            continue;
        }
        d[x.value] = y;
        set[x.value] = true;
    }
    return true;
}

} // namespace cplusplus
