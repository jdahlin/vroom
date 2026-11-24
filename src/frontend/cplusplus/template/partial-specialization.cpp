/*
 * partial-specialization.cpp: [temp.class.spec] Partial specializations: their
 * declarations (of class and variable templates), and [temp.class.spec.match]
 * which one a specialization uses.
 * Split from declared-templates.cpp, which has [temp.pre]'s declarations of
 * templates and what the kinds share.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"
#include "declared-templates.h"

namespace cplusplus
{

/*
 * [temp.class.spec]/1 (with [temp.pre]/1's
 * variable templates): template<class T> const bool v<T, void_t<T&>> =
 * true;: a partial specialization of a variable template, recorded as a
 * class template's is (class_specialization_definition): its arguments in
 * terms of its parameters, which instantiate_variable matches
 * (matching_specialization) and whose declaration it analyses with the
 * deduced arguments.
 */
analysis_result semantic_analysis::variable_partial_specialization(
    SyntaxRef n, SyntaxRef declaration, SyntaxRef id, entity_index qualifier)
{
    entity_index outer = current_scope;
    /* [basic.lookup.qual]/3: what follows the qualified-id is looked up in
       the template's namespace */
    if (qualifier)
        outer = qualifier;
    owned_array<SyntaxRef> parameters;
    /* [temp.param]/14: a partial specialization's packs are deduced,
       wherever they are */
    VTRY(r, template_head_parameters(n, parameters, true, true, true, true));
    lookup_result x;
    entity_index named = 0;
    if (qualifier) {
        SyntaxElement template_name = id.simple_template_id().template_name();
        if (!template_name)
            return not_supported(n, "temp.class.spec");
        x = lookup_in_namespace(qualifier,
                                tokens.identifier(template_name.token()),
                                lookup_filter::any);
        named = x.entity;
        if (!named)
            named = x.unknown_entity;
        if (x.ambiguous)
            return not_supported(n, "temp.class.spec");
    } else {
        template_id_name(id, x, named);
    }
    uint32_t vt = variable_template_of(named);
    if (!vt)
        return not_supported(n, "temp.class.spec");
    const function_template primary = function_templates[vt - 1];
    if (parameters.empty() || function_templates[vt - 1].name != named ||
        entity_table[primary.entity].enclosing != outer)
        return not_supported(n, "temp.class.spec");
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY_SET(r, template_parameter_scope(n, parameters, outer, 0, index, scope,
                                         first));
    owned_array<template_argument> arguments;
    partial_arguments++;
    declaring_specializations++;
    r = template_id_arguments_in(id, vt, scope, arguments);
    partial_arguments--;
    declaring_specializations--;
    if (r != analysis_result::well_formed)
        return r;
    bool dependent = dependent_arguments(arguments);
    bool identity = primary_arguments(n, primary.declaration, arguments, first,
                                      parameters.size());
    /* [temp.class.spec]/9: more specialized than the primary */
    if (!dependent || identity)
        return not_supported(n, "temp.class.spec");
    /* [temp.class.spec]/1: prior instantiations for other arguments are
       unaffected. If this partial specialization would be selected for a
       prior use, no diagnostic is required. Keep the result cached at that
       use and consider this declaration on subsequent instantiations. */
    function_template record;
    record.entity = primary.entity;
    record.declaration = n;
    record.declarator = declaration;
    record.first_parameter = first;
    record.parameter_count = static_cast<uint32_t>(parameters.size());
    record.is_variable = true;
    record.primary = vt;
    record.first_pattern = static_cast<uint32_t>(template_arguments.size());
    record.pattern_count = static_cast<uint32_t>(arguments.size());
    for (const template_argument &a : arguments)
        template_arguments.append(a);
    function_templates.append(record);
    if (!parameters.empty())
        equivalent_partial_specializations(
            static_cast<uint32_t>(function_templates.size()));
    return analysis_result::well_formed;
}

/*
 * [temp.class.spec]/1 (CWG 1980): a partial specialization
 * equivalent to another of the template (each at least as specialized as
 * the other: their arguments the same but for the names of their
 * parameters, void_t<typename T::a> and void_t<typename T::b> both void)
 * redeclares it, which [impl.clang] clang reports as a redefinition and then
 * uses neither; the template is not used ([temp.over.link]/7: functionally
 * equivalent but not equivalent, no diagnostic required)
 */
void semantic_analysis::equivalent_partial_specializations(uint32_t added)
{
    const function_template a = function_templates[added - 1];
    for (uint32_t k = 0; k < function_templates.size(); k++) {
        const function_template &o = function_templates[k];
        if (k + 1 == added || o.primary != a.primary || !o.parameter_count)
            continue;
        /* [temp.over.link]/7: the template-heads' constraints equivalent
           too */
        if (!same_constraints(a.declaration, o.declaration))
            continue;
        bool unknown = false;
        bool forward = class_at_least_as_specialized(added, k + 1, unknown);
        bool backward = class_at_least_as_specialized(k + 1, added, unknown);
        if (forward && backward && !unknown)
            function_templates[a.primary - 1].unusable = true;
    }
}

/*
 * [temp.class.spec]/9: "The specialization shall be more specialized than
 * the primary template": whether partial specialization n's arguments are
 * its parameters (first, count of them) in order, the primary's own,
 * unless n is constrained and the primary is not (/9 Example 1's #2:
 * more constrained, [temp.constr.order]); other cases are
 * [temp.class.order]'s, not analysed.
 */
bool semantic_analysis::primary_arguments(
    SyntaxRef n, SyntaxRef primary,
    const owned_array<template_argument> &arguments, uint32_t first,
    size_t count)
{
    if (arguments.size() != count)
        return false;
    for (uint32_t i = 0; i < count; i++)
        if (!argument_is_parameter(arguments[i],
                                   template_parameter_entities[first + i]))
            return false;
    return !template_head_constrained(n) || template_head_constrained(primary);
}

/*
 * [temp.class.spec]/1: "A partial specialization of a class template
 * provides an alternative definition of the template that is used instead
 * of the primary definition when the arguments in a specialization match
 * those given in the partial specialization."  [temp.expl.spec]/1: template<>
 * with no parameters, the class of those arguments.  A namespace-scope
 * class's definition whose class-head names the primary template, found by
 * the template-id's name, with arguments in terms of the specialization's
 * parameters; instantiate_class chooses it (matching_specialization).
 */
analysis_result semantic_analysis::class_specialization_definition(
    SyntaxRef n, SyntaxRef specifier, const owned_array<SyntaxRef> &parameters)
{
    const char *rule = "temp.class.spec";
    if (parameters.empty())
        rule = "temp.expl.spec";
    /* [temp.class.spec]/10: "The usual access checking rules do not apply
       to non-dependent names used to specify template arguments of the
       simple-template-id of the partial specialization" (as an explicit
       specialization's, [temp.spec]/6; accessible()) */
    flag_set exempt(explicit_declaration_names, true);
    entity_index outer = current_scope;
    /* [temp.class.spec]/5: a partial specialization of a member class
       template is declared in the member's class (member_template_declaration
       leaves defining_class clear); [temp.expl.spec]'s are not analysed
       there ([temp.expl.spec]/2) */
    bool member = entity_table[outer].kind == entity_kind::class_type;
    if ((!is_namespace(outer) && !(member && !parameters.empty())) ||
        defining_class)
        return not_supported(n, rule);
    /* [temp.expl.spec]/1 also declares a specialization without defining
       it: class-key simple-template-id ; (an elaborated-type-specifier),
       whose definition may follow */
    bool defines = specifier.is(syntax_kind::class_specifier);
    /* the specialization's simple-template-id: a name that is a token
       (a plain identifier) is not one */
    auto written = specifier[1];
    if (defines)
        written = class_head_name(specifier[0]);
    if (!written.is_node())
        return not_supported(n, rule);
    bool qualified_head = false;
    if (written.qualified_class_head_name() && written.size() == 2 &&
        written[0].is(syntax_kind::nested_name_specifier) &&
        written[1].is(syntax_kind::simple_template_id)) {
        nominated_scope ns;
        VTRY(nr, nested_name_specifier(written[0].index(), ns));
        if (!is_class_entity(ns.entity) && !is_namespace(ns.entity))
            return not_supported(n, rule);
        outer = ns.entity;
        written = written[1];
        qualified_head = true;
    }
    lookup_result x;
    entity_index named = 0;
    if (qualified_head) {
        if (!written.size() || !written[0].is_token())
            return not_supported(n, rule);
        x = lookup_qualified(outer, tokens.identifier(written[0].token()),
                             lookup_filter::any);
        if (x.unknown)
            named = x.unknown_entity;
        else
            named = x.entity;
    } else if (!template_id_name(written, x, named)) {
        return not_supported(n, rule);
    }
    uint32_t ct = class_template_of(named);
    /* [temp.class.spec]/6, [temp.expl.spec]/3: declared where the
       primary template is, by its name (not an injected-class-name) */
    if (!ct || function_templates[ct - 1].name != named)
        return not_supported(n, rule);
    /* ... or in a namespace the primary's inline namespaces are members of
       ([namespace.memdef]/3, [temp.expl.spec]/2) */
    entity_index home =
        entity_table[function_templates[ct - 1].entity].enclosing;
    while (home != outer && home && is_namespace(home) &&
           entity_table[home].inline_)
        home = entity_table[home].enclosing;
    if (home != outer)
        return not_supported(n, rule);
    const function_template primary = function_templates[ct - 1];
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY(r, template_parameter_scope(n, parameters, outer, 0, index, scope,
                                     first));
    /* the arguments, in the parameters' scope; what they record for
       claims is the template's */
    owned_array<template_argument> arguments;
    partial_arguments++;
    declaring_specializations++;
    r = template_id_arguments_in(written, ct, scope, arguments);
    partial_arguments--;
    declaring_specializations--;
    if (r != analysis_result::well_formed)
        return r;
    bool dependent = dependent_arguments(arguments);
    bool identity = primary_arguments(n, primary.declaration, arguments, first,
                                      parameters.size());
    /* [temp.expl.spec]: no dependent argument; [temp.class.spec]/9: "The
       specialization shall be more specialized than the primary template"
       (primary_arguments) */
    if (parameters.empty() == dependent || identity)
        return not_supported(n, rule);
    /* [temp.expl.spec]/7, [temp.class.spec]/1: one declared after a
       specialization it would be used for was instantiated makes the
       program ill-formed (no diagnostic required); any instantiation
       before it is not analysed.  One of an explicit specialization is
       that specialization's own (declared, it is defined at its next
       use), never another's.  One named but not defined yet (a
       specialization's head, instantiate_class) is not instantiated */
    /* only an instantiation whose arguments this one matches would have
       used it: [temp.class.spec]/1, [temp.class.spec.match]/2 */
    for (const instantiation_record &r : instantiations)
        if (r.template_entity == primary.entity && r.specialization &&
            entity_table[r.specialization].kind == entity_kind::class_type &&
            (entity_table[r.specialization].defined || r.defining) &&
            !(r.pattern &&
              !function_templates[r.pattern - 1].parameter_count) &&
            r.argument_count == arguments.size()) {
            owned_array<template_argument> d;
            owned_array<bool> set;
            for (size_t i = 0; i < parameters.size(); i++) {
                d.append(template_argument());
                set.append(false);
            }
            bool match = true;
            matching_partial = true;
            for (size_t i = 0; i < arguments.size() && match; i++) {
                const template_argument &y =
                    template_arguments[r.first_argument + i];
                if (arguments[i].kind == template_argument::kind_t::pack &&
                    y.kind == template_argument::kind_t::pack)
                    match = deduce_pack_argument(arguments[i], y, d, set);
                else
                    match = deduce_argument(arguments[i], y, d, set);
            }
            matching_partial = false;
            if (match)
                return not_supported(n, rule);
        }
    /* [temp.class.spec]/1: a partial specialization declared only is
       recorded by its definition */
    if (!defines && !parameters.empty())
        return analysis_result::well_formed;
    function_template record;
    record.entity = primary.entity;
    record.declaration = n;
    record.definition = 0;
    if (defines)
        record.definition = specifier;
    record.first_parameter = first;
    record.parameter_count = static_cast<uint32_t>(parameters.size());
    record.is_class = true;
    record.primary = ct;
    record.first_pattern = static_cast<uint32_t>(template_arguments.size());
    record.pattern_count = static_cast<uint32_t>(arguments.size());
    /* a second explicit specialization of the same arguments: a
       redefinition ([basic.def.odr]/1) */
    for (function_template &o : function_templates) {
        if (o.primary != ct || o.parameter_count || !parameters.empty())
            continue;
        bool same = true;
        for (uint32_t i = 0; i < o.pattern_count && same; i++)
            same = template_arguments[o.first_pattern + i] == arguments[i];
        if (!same)
            continue;
        /* [basic.def]/1: declared again, or defined after its
           declaration (the instantiations that named it define it at
           their next use, instantiate_class) */
        if (!defines)
            return analysis_result::well_formed;
        if (o.definition)
            return not_supported(n, rule);
        o.declaration = n;
        o.definition = specifier;
        return analysis_result::well_formed;
    }
    for (const template_argument &a : arguments)
        template_arguments.append(a);
    function_templates.append(record);
    if (!parameters.empty())
        equivalent_partial_specializations(
            static_cast<uint32_t>(function_templates.size()));
    return analysis_result::well_formed;
}

/*
 * [temp.class.spec.match]/1: "When a class template is used in a context
 * that requires an instantiation of the class, it is necessary to
 * determine whether the instantiation is to be generated using the primary
 * template or one of the partial specializations."  /2: "A partial
 * specialization matches a given actual template argument list if the
 * template arguments of the partial specialization can be deduced from the
 * actual template argument list" (exactly: [temp.deduct.type]/1's "make P
 * ... identical to A").  An explicit specialization of the arguments is
 * the specialization itself.  More than one partial specialization
 * matching is /1.2's partial ordering ([temp.class.order],
 * partial-ordering.cpp).
 */
/*
 * [temp.class.spec.match]/2-3 (C++20 [temp.spec.partial.match]): "If the
 * template arguments of a partial specialization cannot be deduced
 * because of the structure of its template-parameter-list and the
 * template-id, the program is ill-formed" ... "the deduced template
 * arguments are substituted into" the specialization's arguments: they
 * shall be valid and the same as the use's.  The specialization's
 * template-id analysed again with its parameters bound to what deduction
 * found; a failure there is no match ([temp.deduct]/8), the arguments
 * that aliases fold kept (void_t<typename T::type> of CWG 1558)
 */
analysis_result semantic_analysis::substitute_specialization(
    uint32_t specialization, const owned_array<template_argument> &deduced,
    const template_argument *arguments, uint32_t count, bool &substituted)
{
    substituted = false;
    const function_template s = function_templates[specialization - 1];
    SyntaxRef id = 0;
    if (s.is_variable) {
        id = variable_specialization_id(s.declarator);
        /* named by a qualified template-id, N::v<args> ([dcl.meaning]/1;
           it was checked where the specialization was declared) */
        if (!id && s.declarator.size() == 3 &&
            s.declarator[1].is(syntax_kind::init_declarator) &&
            s.declarator[1][0].qualified_id() &&
            s.declarator[1][0].qualified_id().size() == 2)
            id = s.declarator[1][0].qualified_id()[1].index();
    } else {
        SyntaxRef head = s.definition[0].index();
        id = class_head_name(head).index();
    }
    entity_index scope = entity_table.make(entity_kind::block_scope, 0,
                                           entity_table[s.entity].enclosing);
    for (uint32_t i = 0; i < s.parameter_count; i++)
        bind_template_argument(
            scope, template_parameter_entities[s.first_parameter + i],
            deduced[i]);
    substitution_mark mark = mark_substitution();
    owned_array<template_argument> again;
    token_index saved_horizon = lookup_horizon;
    lookup_horizon = s.declaration.first_token();
    substituting++;
    partial_arguments++;
    declaring_specializations++;
    analysis_result r = template_id_arguments_in(id, s.primary, scope, again);
    partial_arguments--;
    declaring_specializations--;
    substituting--;
    lookup_horizon = saved_horizon;
    if (r == analysis_result::ill_formed && undo_substitution(mark))
        return analysis_result::well_formed;
    if (r != analysis_result::well_formed)
        return r;
    if (again.size() != count)
        return analysis_result::well_formed;
    for (uint32_t i = 0; i < count; i++)
        if (!(again[i] == arguments[i]))
            return analysis_result::well_formed;
    substituted = true;
    return analysis_result::well_formed;
}

/* [temp.deduct.type]/8: a partial specialization's argument x against
   the argument y it is matched with, the parameters it names deduced
   into d (set) */
bool semantic_analysis::deduce_argument(const template_argument &x,
                                        const template_argument &y,
                                        owned_array<template_argument> &d,
                                        owned_array<bool> &set)
{
    if (x.kind != y.kind)
        return false;
    if (x.kind == template_argument::kind_t::type)
        return deduce_type(x.type, y.type, d, set, true);
    if (!x.template_entity)
        return x == y;
    /* [temp.deduct.type]/5.5: a value-dependent argument other than a
       parameter's name (is_integral<T>::value) is a non-deduced context:
       the match is the one of the substituted arguments
       (substitute_specialization) */
    if (entity_table[x.template_entity].kind == entity_kind::dependent_value)
        return true;
    if (set[x.value])
        return d[x.value] == y;
    d[x.value] = y;
    set[x.value] = true;
    return true;
}

/* [temp.deduct.type]/9: the elements of a pack argument x (the
   partial specialization's, <I1, In...>) against the elements of y, each
   in turn; an element that is the expansion of the specialization's own
   pack (count UINT32_MAX, its last) takes the rest of y's as its pack */
bool semantic_analysis::deduce_pack_argument(const template_argument &x,
                                             const template_argument &y,
                                             owned_array<template_argument> &d,
                                             owned_array<bool> &set)
{
    for (uint32_t k = 0; k < x.count; k++) {
        const template_argument e = pack_arguments[x.value + k];
        if (e.kind == template_argument::kind_t::pack &&
            e.count == UINT32_MAX) {
            /* /5.7: a pack expansion not at the end is a non-deduced
               context: not analysed; nor one of a pattern (no position) */
            if (k + 1 != x.count || e.value == UINT32_MAX) {
                deduction_unknowns++;
                return false;
            }
            if (k > y.count)
                return false;
            owned_array<template_argument> rest;
            for (uint32_t r = k; r < y.count; r++)
                rest.append(pack_arguments[y.value + r]);
            template_argument p =
                make_pack(rest.data(), static_cast<uint32_t>(rest.size()));
            uint64_t at = e.value;
            if (set[at])
                return d[at] == p;
            d[at] = p;
            set[at] = true;
            return true;
        }
        if (k >= y.count ||
            !deduce_argument(e, pack_arguments[y.value + k], d, set))
            return false;
    }
    return x.count == y.count;
}

analysis_result semantic_analysis::matching_specialization(
    SyntaxRef at, uint32_t class_template, const template_argument *arguments,
    uint32_t count, uint32_t &pattern, owned_array<template_argument> &deduced)
{
    pattern = 0;
    deduced.clear();
    uint32_t matches = 0;
    /* the partial specializations that match, and their deduced
       arguments' place in all */
    owned_array<uint32_t> found, first_deduced;
    owned_array<template_argument> all;
    for (uint32_t k = 0; k < function_templates.size(); k++) {
        const function_template s = function_templates[k];
        if (s.primary != class_template || s.pattern_count != count)
            continue;
        owned_array<template_argument> d;
        owned_array<bool> set;
        for (uint32_t i = 0; i < s.parameter_count; i++) {
            d.append(template_argument());
            set.append(false);
        }
        uint32_t unknowns = deduction_unknowns;
        bool match = true;
        matching_partial = true;
        for (uint32_t i = 0; i < count && match; i++) {
            const template_argument x = template_arguments[s.first_pattern + i];
            const template_argument &y = arguments[i];
            if (x.kind == template_argument::kind_t::pack &&
                y.kind == template_argument::kind_t::pack)
                match = deduce_pack_argument(x, y, d, set);
            else
                match = deduce_argument(x, y, d, set);
        }
        matching_partial = false;
        if (deduction_unknowns != unknowns)
            return not_supported(at, "temp.deduct.type");
        if (!match)
            continue;
        if (!s.parameter_count) {
            pattern = k + 1;
            deduced.clear();
            return analysis_result::well_formed;
        }
        /* [temp.class.spec.match]/2: a parameter the arguments do not
           determine is not deduced (clang's "can never be used") */
        for (uint32_t i = 0; i < s.parameter_count; i++)
            if (!set[i])
                return not_supported(at, "temp.class.spec");
        bool substituted = false;
        VTRY(again, substitute_specialization(k + 1, d, arguments, count,
                                              substituted));
        if (!substituted)
            continue;
        /* [temp.class.spec.match]/2: "and the deduced template arguments
           satisfy the associated constraints of the partial
           specialization, if any" */
        bool satisfied = true;
        VTRY(c, template_constraints_satisfied(k + 1, d, satisfied));
        if (!satisfied)
            continue;
        matches++;
        found.append(k + 1);
        first_deduced.append(static_cast<uint32_t>(all.size()));
        for (const template_argument &a : d)
            all.append(a);
    }
    if (!matches)
        return analysis_result::well_formed;
    /* [temp.class.spec.match]/1.2: "If more than one matching
       specialization is found, the partial order rules ([temp.class.order])
       are used to determine whether one of the specializations is more
       specialized than the others.  If none of the specializations is more
       specialized than all of the other matching specializations, then the
       use of the class template is ambiguous and the program is
       ill-formed" (the ambiguity, or an order analysis cannot tell:
       unsupported) */
    size_t chosen = found.size();
    for (size_t i = 0; i < found.size() && chosen == found.size(); i++) {
        bool best = true;
        for (size_t j = 0; j < found.size() && best; j++) {
            if (i == j)
                continue;
            bool unknown = false;
            bool forward =
                class_at_least_as_specialized(found[i], found[j], unknown);
            bool backward =
                class_at_least_as_specialized(found[j], found[i], unknown);
            if (unknown)
                return not_supported(at, "temp.class.order");
            best = forward && !backward;
        }
        if (best)
            chosen = i;
    }
    if (chosen == found.size())
        return not_supported(at, "temp.class.order");
    pattern = found[chosen];
    deduced.clear();
    for (uint32_t i = 0; i < function_templates[pattern - 1].parameter_count;
         i++)
        deduced.append(all[first_deduced[chosen] + i]);
    return analysis_result::well_formed;
}

} // namespace cplusplus
