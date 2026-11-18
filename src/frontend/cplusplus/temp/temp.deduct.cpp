/*
 * temp.deduct.cpp: template argument deduction ([temp.deduct]): from a
 * type ([temp.deduct.type]) and from a function call
 * ([temp.deduct.call]), with the explicitly specified arguments and the
 * defaults.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

qualified_type semantic_analysis::remove_cv(qualified_type t, uint32_t cv)
{
    const struct type record = type_table[t.unqualified()];
    if (record.kind != type_kind::array)
        return qualified_type::make(t.unqualified(), t.cv() & ~cv);
    qualified_type element = remove_cv(record.operand, cv);
    if (record.bound_known)
        return qualified_type::make(type_table.array_of(element, record.bound),
                                    t.cv() & ~cv);
    return qualified_type::make(type_table.array_of_unknown_bound_of(element),
                                t.cv() & ~cv);
}

/*
 * [temp.deduct.type]/1: "Template arguments can be deduced in several
 * different contexts, but in each case a type that is specified in terms
 * of template parameters (call it P) is compared with an actual type (call
 * it A), and an attempt is made to find template argument values ... that
 * will make P, after substitution of the deduced values (call it the
 * deduced A), compatible with A."  /8's forms T, cv T, T*, T&, T&&, T[N]
 * and function types.  False when P and A do not match or deductions
 * conflict.
 */
bool semantic_analysis::deduce_type(qualified_type p, qualified_type a,
                                    owned_array<template_argument> &deduced,
                                    owned_array<bool> &set, bool exact)
{
    const struct type &pr = type_table[p.unqualified()];
    if (pr.kind == type_kind::template_parameter) {
        /* (P's cv-qualifiers beyond A's: [temp.deduct.call]/4.1's "more
           cv-qualified" deduced A, or 4.2's qualification conversion; the
           call's conversions check the rest.  An array's cv-qualifiers are
           its elements', [basic.type.qualifier]/3) */
        uint32_t acv = cv_of(type_table, a);
        if (exact && (p.cv() & ~acv))
            return false;
        uint32_t i = static_cast<uint32_t>(entity_table[pr.entity].value);
        qualified_type value = remove_cv(a, p.cv());
        if (entity_table[pr.entity].value >= deduced.size())
            return false;
        if (set[i] && !(deduced[i].type == value))
            return false;
        deduced[i].type = value;
        set[i] = true;
        return true;
    }
    if (!depends_on_template_parameter(p))
        return p == a;
    if (pr.kind == type_kind::dependent &&
        entity_table[pr.entity].kind == entity_kind::dependent_member) {
        /* [temp.deduct.type]/5.1: "The nested-name-specifier of a type that
           was specified using a qualified-id" is a non-deduced context;
           the argument's conversion is checked on the specialization.
           Comparing two declarations ([temp.over.link]/7), the same name
           of an equivalent qualifier */
        if (!exact)
            return true;
        const struct type &ar = type_table[a.unqualified()];
        if (ar.kind != type_kind::dependent || p.cv() != a.cv() ||
            entity_table[ar.entity].kind != entity_kind::dependent_member ||
            entity_table[ar.entity].name != entity_table[pr.entity].name)
            return false;
        return deduce_type(
            qualified_type::make(
                static_cast<type_index>(entity_table[pr.entity].value),
                cv_none),
            qualified_type::make(
                static_cast<type_index>(entity_table[ar.entity].value),
                cv_none),
            deduced, set, exact);
    }
    if (pr.kind == type_kind::dependent) {
        /* [temp.deduct.type]/8's TT<T>: a class template's specialization,
           whose arguments are deduced from those of A's, a
           specialization of the same template */
        const struct type &ar = type_table[a.unqualified()];
        /* ([temp.deduct.call]/4.1: P may be more cv-qualified) */
        if (ar.kind != type_kind::class_type || (a.cv() & ~p.cv()) ||
            (exact && p.cv() != a.cv()) ||
            !entity_table[ar.entity].instantiation)
            return false;
        entity_index pt = specialized_template(pr.entity);
        entity_index at = specialized_template(ar.entity);
        if (template_template_parameter(pt)) {
            /* a pack among its arguments (TT<T, Rest...>), whose template
               may take fewer parameters: not analysed */
            for (uint32_t i = 0; i < specialization_argument_count(pr.entity);
                 i++)
                if (specialization_template_argument(pr.entity, i).kind ==
                    template_argument::kind_t::pack) {
                    deduction_unknowns++;
                    return false;
                }
            /* /8's TT<T>: TT a template template-parameter, deduced as
               A's template when their parameters are the same
               ([temp.arg.template]/3's matching, nothing more) */
            const function_template x =
                function_templates[entity_table[pt].instantiation - 1];
            const function_template y =
                function_templates[entity_table[at].instantiation - 1];
            uint64_t k = entity_table[pt].value;
            if (y.is_parameter || y.primary ||
                x.parameter_count != y.parameter_count ||
                !same_template_parameters(x.first_parameter, y.first_parameter,
                                          x.parameter_count) ||
                k >= deduced.size())
                return false;
            template_argument named;
            named.kind = template_argument::kind_t::template_name;
            named.value = at;
            if (set[k] && !(deduced[k] == named))
                return false;
            deduced[k] = named;
            set[k] = true;
        } else if (at != pt) {
            return false;
        }

        uint32_t count = specialization_argument_count(pr.entity);
        for (uint32_t i = 0; i < count; i++) {
            const template_argument &x =
                specialization_template_argument(pr.entity, i);
            const template_argument &y =
                specialization_template_argument(ar.entity, i);
            if (x.kind == template_argument::kind_t::type) {
                if (!deduce_type(x.type, y.type, deduced, set, exact))
                    return false;
                continue;
            }
            /* /9: a pack's elements in turn, the pattern's own pack
               (Ts...) the rest */
            if (x.kind == template_argument::kind_t::pack) {
                if (y.kind != template_argument::kind_t::pack ||
                    !deduce_pack_argument(x, y, deduced, set))
                    return false;
                continue;
            }
            if (!x.template_entity) {
                if (!(x == y))
                    return false;
                continue;
            }
            /* /5.3: "A non-type template argument ... in which a
               subexpression references a template parameter" is a
               non-deduced context */
            if (entity_table[x.template_entity].kind ==
                entity_kind::dependent_value)
                continue;
            /* /8's TT<i>: a non-type parameter's value from A's (of the
               parameter's type: the template's parameter is the same) */
            uint64_t k = x.value;
            if (k >= deduced.size() || y.kind != x.kind)
                return false;
            if (set[k] && !(deduced[k] == y))
                return false;
            deduced[k] = y;
            set[k] = true;
        }
        return true;
    }
    const struct type &ar = type_table[a.unqualified()];
    if (pr.kind != ar.kind || p.cv() != a.cv())
        return false;
    switch (pr.kind) {
    case type_kind::pointer:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
    case type_kind::pack_expansion:
        return deduce_type(pr.operand, ar.operand, deduced, set, exact);
    case type_kind::array:
        if (pr.bound_known != ar.bound_known || pr.bound != ar.bound)
            return false;
        return deduce_type(pr.operand, ar.operand, deduced, set, exact);
    case type_kind::function: {
        if (pr.variadic != ar.variadic || pr.non_throwing != ar.non_throwing ||
            pr.parameter_count != ar.parameter_count)
            return false;
        if (!deduce_type(pr.operand, ar.operand, deduced, set, exact))
            return false;
        const qualified_type *pp = type_table.parameters(p.unqualified());
        const qualified_type *ap = type_table.parameters(a.unqualified());
        for (uint32_t i = 0; i < pr.parameter_count; i++)
            if (!deduce_type(pp[i], ap[i], deduced, set, exact))
                return false;
        return true;
    }
    default:
        return false;
    }
}

/*
 * [temp.deduct]/1: "When a function template specialization is referenced,
 * all of the template arguments shall have values.  The values can be
 * explicitly specified or, in some cases, be deduced from the use or
 * obtained from default template-arguments."  [temp.deduct.call]: from a
 * call's arguments.  No rules yet.
 */
/* a P and A (adjusted, [temp.deduct.call]/2-3) that deduce_type does not
   match but that /4 may still: A of a class type or a pointer to one
   (/4.3, a derived class), a pointer to a function (/4.2's function
   pointer conversion), P a pointer to a pointer (/4.2's qualification
   conversion of more than one level), P of a kind deduce_type has no
   rules for */
static bool deduction_may_succeed(const types &table, qualified_type p,
                                  qualified_type a)
{
    const struct type &ar = table[a.unqualified()];
    const struct type &pr = table[p.unqualified()];
    /* (/4.3 needs P a class, a simple-template-id, or a pointer to one;
       /4.2 a pointer P for a pointer A) */
    if (ar.kind == type_kind::class_type)
        return pr.kind == type_kind::dependent;
    if (ar.kind == type_kind::dependent)
        return true;
    if (ar.kind == type_kind::pointer) {
        type_kind k = table[ar.operand.unqualified()].kind;
        if (k == type_kind::class_type || k == type_kind::function ||
            k == type_kind::dependent)
            return pr.kind == type_kind::pointer ||
                   pr.kind == type_kind::dependent;
    }
    switch (pr.kind) {
    case type_kind::template_parameter:
    case type_kind::fundamental:
    case type_kind::enumeration:
    case type_kind::array:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
        return false;
    case type_kind::pointer: {
        /* (A with fewer levels of pointers than P never matches it) */
        qualified_type x = p, y = a;
        while (table[x.unqualified()].kind == type_kind::pointer &&
               table[y.unqualified()].kind == type_kind::pointer) {
            x = table[x.unqualified()].operand;
            y = table[y.unqualified()].operand;
        }
        if (table[x.unqualified()].kind == type_kind::pointer) {
            type_kind k = table[y.unqualified()].kind;
            return k == type_kind::class_type || k == type_kind::dependent ||
                   k == type_kind::function;
        }
        type_kind k = table[pr.operand.unqualified()].kind;
        return k != type_kind::template_parameter &&
               k != type_kind::fundamental && k != type_kind::enumeration;
    }
    default:
        return true;
    }
}

/* [temp.deduct.call]/2-3: the P and A deduce_type compares, for a
   parameter of type p and an argument of type a and value category c */
void semantic_analysis::adjust_call_deduction(qualified_type &p,
                                              qualified_type &a,
                                              value_category c)
{
    const struct type argument = type_table[a.unqualified()];
    const struct type pr = type_table[p.unqualified()];
    if (pr.kind == type_kind::rvalue_reference &&
        type_table[pr.operand.unqualified()].kind ==
            type_kind::template_parameter &&
        !pr.operand.cv() && c == value_category::lvalue) {
        /* [temp.deduct.call]/3: "A forwarding reference is an rvalue
           reference to a cv-unqualified template parameter ... If P
           is a forwarding reference and the argument is an lvalue,
           the type "lvalue reference to A" is used in place of A for
           type deduction." */
        p = pr.operand;
        a = qualified_type::make(type_table.lvalue_reference_to(a), cv_none);
    } else if (pr.kind == type_kind::lvalue_reference ||
               pr.kind == type_kind::rvalue_reference) {
        /* /3: "If P is a reference type, the type referred to by P is
           used for type deduction." */
        p = pr.operand;
    } else {
        /* /2: "If P is not a reference type: If A is an array type,
           the pointer type produced by the array-to-pointer standard
           conversion is used in place of A for type deduction;
           otherwise, If A is a function type, the pointer type
           produced by the function-to-pointer standard conversion is
           used in place of A for type deduction; otherwise, If A is a
           cv-qualified type, the top-level cv-qualifiers of A's type
           are ignored for type deduction."  /3: "If P is a
           cv-qualified type, the top-level cv-qualifiers of P's type
           are ignored" */
        if (argument.kind == type_kind::array)
            a = qualified_type::make(type_table.pointer_to(argument.operand),
                                     cv_none);
        else if (argument.kind == type_kind::function)
            a = qualified_type::make(type_table.pointer_to(a), cv_none);
        else
            a = qualified_type::make(a.unqualified(), cv_none);
        p = qualified_type::make(p.unqualified(), cv_none);
    }
}

analysis_result semantic_analysis::deduce_from_call(
    node_index at, uint32_t function_template,
    const expression_meaning *arguments, uint32_t count,
    owned_array<template_argument> &deduced, bool *failed)
{
    if (failed)
        *failed = false;
    const struct function_template &ft =
        function_templates[function_template - 1];
    qualified_type type = entity_table[ft.entity].type;
    const struct type f = type_table[type.unqualified()];
    const qualified_type *parameters =
        type_table.parameters(type.unqualified());
    /* [temp.deduct.call]/1: "For a function parameter pack that occurs at
       the end of the parameter-declaration-list, deduction is performed
       for each remaining argument of the call, taking the type P of the
       declarator-id of the function parameter pack as the corresponding
       function template parameter type.  Each deduction deduces template
       arguments for subsequent positions in the template parameter packs
       expanded by the function parameter pack." */
    uint32_t fixed = f.parameter_count;
    if (fixed && type_table[parameters[fixed - 1].unqualified()].kind ==
                     type_kind::pack_expansion)
        fixed--;
    if (f.variadic)
        return not_supported(at, "temp.deduct.call");
    if (count < fixed || (fixed == f.parameter_count && count != fixed)) {
        /* [over.match.viable]/2: no default argument is analysed in a
           template, so a template of another number of parameters
           cannot be called with these arguments */
        if (!failed)
            return not_supported(at, "temp.deduct.call");
        *failed = true;
        return analysis_result::well_formed;
    }
    /* the template parameter pack, wherever it is ([temp.param]/14: the
       parameters after it deduced or with defaults) */
    uint32_t pack = ft.parameter_count;
    for (uint32_t i = 0; i < ft.parameter_count; i++)
        if (template_parameter_is_pack(ft.first_parameter + i))
            pack = i;
    /* [temp.arg.explicit]/3: "A template argument list may be specified
       when referring to a specialization of a function template"; /6:
       "The template argument list shall not specify more
       template-arguments than there are corresponding
       template-parameters" (clang: no candidate) */
    uint32_t specified = static_cast<uint32_t>(deduced.size());
    if (specified > ft.parameter_count || specified > pack)
        /* (arguments of a pack given: [temp.arg.explicit]/9's sequence,
           extended by deduction) */
        return not_supported(at, "temp.arg.explicit");
    owned_array<template_argument> values;
    owned_array<bool> set;
    for (uint32_t i = 0; i < ft.parameter_count; i++) {
        template_argument v;
        if (i < specified)
            v = deduced[i];
        values.append(v);
        set.append(i < specified);
    }
    owned_array<template_argument> elements;
    for (uint32_t i = 0; i < count; i++) {
        qualified_type p = parameters[i];
        if (i >= fixed)
            p = type_table[parameters[fixed].unqualified()].operand;
        /* /7: "Implicit conversions will be performed on a function
           argument to convert it to the type of the corresponding function
           parameter if the parameter type contains no template-parameters
           that participate in template argument deduction", which those
           explicitly specified do not */
        if (!depends_on_parameter_from(p, specified))
            continue;
        qualified_type a = arguments[i].type;
        const struct type ar = type_table[a.unqualified()];
        if (ar.kind == type_kind::none || ar.kind == type_kind::placeholder ||
            ar.kind == type_kind::dependent)
            return not_supported(at, "temp.deduct.call");
        adjust_call_deduction(p, a, arguments[i].category);
        /* a parameter that does not match: deduction fails, which
           overload resolution of the function templates of the name and
           its other functions decides; neither is analysed yet */
        uint32_t unknowns = deduction_unknowns;
        if (i < fixed) {
            if (deduce_type(p, a, values, set))
                continue;
            if (deduction_unknowns != unknowns)
                return not_supported(at, "temp.deduct.type");
            /* [temp.over]/1: "If, for a given function template, argument
               deduction fails or the synthesized function template
               specialization would be ill-formed, no such function is
               added to the set of candidate functions for that template" */
            if (!failed || deduction_may_succeed(type_table, p, a))
                return not_supported(at, "temp.deduct.call");
            *failed = true;
            return analysis_result::well_formed;
        }
        /* the pack's next element, the other parameters the pattern names
           the same in each */
        owned_array<template_argument> one;
        owned_array<bool> one_set;
        for (uint32_t k = 0; k < values.size(); k++) {
            one.append(values[k]);
            one_set.append(set[k] && k != pack);
        }
        if (pack >= one.size())
            return not_supported(at, "temp.deduct.call");
        if (!deduce_type(p, a, one, one_set)) {
            if (deduction_unknowns != unknowns)
                return not_supported(at, "temp.deduct.type");
            if (!failed || deduction_may_succeed(type_table, p, a))
                return not_supported(at, "temp.deduct.call");
            *failed = true;
            return analysis_result::well_formed;
        }
        if (!one_set[pack])
            return not_supported(at, "temp.deduct.call");
        for (uint32_t k = 0; k < values.size(); k++)
            if (k != pack && one_set[k] && !set[k]) {
                values[k] = one[k];
                set[k] = true;
            }
        elements.append(one[pack]);
    }
    /* [temp.arg.explicit]/4: "A trailing template parameter pack not
       otherwise deduced will be deduced as an empty sequence of template
       arguments." */
    if (pack < ft.parameter_count) {
        values[pack] =
            make_pack(elements.data(), static_cast<uint32_t>(elements.size()));
        set[pack] = true;
    }
    /* [temp.deduct]/5: "If a template argument has not been deduced and
       its corresponding template parameter has a default argument, the
       template argument is determined by substituting the template
       arguments determined for preceding template parameters into the
       default argument"; /1's "all of the template arguments shall have
       values" otherwise */
    analysis_result r =
        default_template_arguments(at, function_template, values, set);
    if (r != analysis_result::well_formed)
        return r;
    /* [temp.deduct]/5: "If the function template has associated
       constraints ([temp.constr.decl]), those constraints are checked for
       satisfaction ([temp.constr.constr]).  If the constraints are not
       satisfied, type deduction fails." */
    bool satisfied = true;
    r = template_constraints_satisfied(function_template, values, satisfied);
    if (r != analysis_result::well_formed)
        return r;
    if (!satisfied) {
        if (!failed)
            return not_supported(at, "temp.deduct");
        *failed = true;
        return analysis_result::well_formed;
    }
    deduced.clear();
    for (const template_argument &x : values)
        deduced.append(x);
    return analysis_result::well_formed;
}

} // namespace cplusplus
