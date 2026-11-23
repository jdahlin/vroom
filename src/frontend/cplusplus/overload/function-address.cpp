/*
 * function-address.cpp: [over.over] Address of an overload set.
 *
 * [over.over]/1: "A use of a function name without arguments is resolved
 * to a function, a pointer to function, or a pointer to member function for
 * a specific function that is chosen from a set of selected functions
 * determined based on the target type required in the context (if any), as
 * described below. ... The function name can be preceded by the & operator.
 * [Note 1: Any redundant set of parentheses surrounding the function name is
 * ignored ([expr.prim.paren]).]"  /2: "a non-template function with type F
 * is selected for the function type FT of the target type if F (after
 * possibly applying the function pointer conversion ([conv.fctptr])) is
 * identical to FT."  /4: "Non-member functions and static member functions
 * match targets of function pointer type or reference to function type."
 *
 * The targets analysed: an object or reference initialized, the left side
 * of an assignment, a parameter of a function called, the return value and
 * an explicit type conversion; the names: an unqualified name of an overload
 * set of non-member functions or of function templates ([temp.deduct.funcaddr]);
 * a template-id naming one function template is function_template_id_value's.
 * An argument that is such a name stays an overload set (expression_meaning::
 * overload_name) through overload resolution, where it converts to a
 * parameter of function type by overload_set_sequence, and is resolved
 * for the parameter of the function chosen.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

/* the operand of redundant parentheses */
SyntaxElement without_parentheses(SyntaxElement e)
{
    while (e.is(syntax_kind::primary_expression) && e.size() == 3 &&
           e[0].is(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        e = e[1];
    return e;
}

} // namespace

entity_index semantic_analysis::named_overload_set(SyntaxElement e,
                                                   token_index &at) const
{
    auto name = without_parentheses(e);
    if (name.is(syntax_kind::unary_expression) && name.size() == 2 &&
        name[0].is(preprocessing_op_or_punc::AMPERSAND))
        name = without_parentheses(name[1]);
    if (!name.is_identifier())
        return 0;
    at = name.token();
    lookup_result found =
        lookup_unqualified(tokens.identifier(at), lookup_filter::any);
    if (!found.entity || found.unknown || found.ambiguous)
        return 0;
    const entity &first = entity_table[found.entity];
    if (first.kind != entity_kind::function || !first.next_overload)
        return 0;
    return found.entity;
}

bool semantic_analysis::function_set_at(token_index at, function_set &out) const
{
    out.at = at;
    lookup_result found =
        lookup_unqualified(tokens.identifier(at), lookup_filter::any);
    if (found.ambiguous)
        return false;
    if (found.unknown) {
        /* function templates of the name, and the functions beside them */
        if (!function_template_of(found.unknown_entity))
            return false;
        function_template_overloads(found.unknown_entity, out.templates);
        for (const template_name_function &x : template_name_functions)
            if (x.name == found.unknown_entity)
                out.functions.append(x.function);
        if (found.entity)
            overload_set(found.entity, out.functions);
        return true;
    }
    if (!found.entity)
        return false;
    const entity &first = entity_table[found.entity];
    if (first.kind != entity_kind::function || !first.next_overload)
        return false;
    overload_set(found.entity, out.functions);
    return true;
}

bool semantic_analysis::named_function_set(SyntaxElement e,
                                           function_set &out) const
{
    auto name = without_parentheses(e);
    if (name.is(syntax_kind::unary_expression) && name.size() == 2 &&
        name[0].is(preprocessing_op_or_punc::AMPERSAND))
        name = without_parentheses(name[1]);
    if (name.is_identifier())
        return function_set_at(name.token(), out);
    if (name.is(syntax_kind::qualified_id) && name.size() == 2 &&
        name[0].is(syntax_kind::nested_name_specifier) &&
        name[1].is_identifier()) {
        /* C::f or N::f: simple nested-name-specifier naming a class or
           namespace */
        SyntaxElement qual = name[0];
        if (qual.size() != 2 || !qual[0].is_identifier())
            return false;
        lookup_result scope = lookup_unqualified(
            tokens.identifier(qual[0].token()), lookup_filter::any);
        if (!scope.entity || scope.ambiguous || scope.unknown)
            return false;
        entity_index c = scope.entity;
        if (!is_class_entity(c) && !is_namespace(c))
            return false;
        token_index at = name[1].token();
        lookup_result found =
            lookup_qualified(c, tokens.identifier(at), lookup_filter::any);
        if (found.ambiguous)
            return false;
        out.at = at;
        if (found.unknown) {
            if (!function_template_of(found.unknown_entity) &&
                !member_function_template_of(found.unknown_entity))
                return false;
            function_template_overloads(found.unknown_entity, out.templates);
            for (const template_name_function &x : template_name_functions)
                if (x.name == found.unknown_entity)
                    out.functions.append(x.function);
            if (found.entity)
                overload_set(found.entity, out.functions);
            return !out.templates.empty() || !out.functions.empty();
        }
        if (!found.entity)
            return false;
        const entity &first = entity_table[found.entity];
        if (first.kind != entity_kind::function)
            return false;
        if (!first.next_overload && !first.member_templates)
            return false;
        overload_set(found.entity, out.functions);
        if (first.member_templates)
            function_template_overloads(first.member_templates, out.templates);
        return true;
    }
    return false;
}

bool semantic_analysis::overload_set_operand(SyntaxElement e,
                                             expression_meaning &m) const
{
    function_set set;
    if (!named_function_set(e, set))
        return false;
    m = expression_meaning();
    m.overload_name = set.at;
    return true;
}

bool semantic_analysis::function_target(qualified_type target,
                                        qualified_type &ft,
                                        entity_index *member_class) const
{
    if (member_class)
        *member_class = 0;
    ft = target;
    const struct type &t = type_table[target.unqualified()];
    if (t.kind == type_kind::pointer || t.kind == type_kind::lvalue_reference ||
        t.kind == type_kind::rvalue_reference) {
        ft = t.operand;
    } else if (t.kind == type_kind::pointer_to_member) {
        ft = t.operand;
        if (member_class)
            *member_class = t.entity;
    }
    return type_table[ft.unqualified()].kind == type_kind::function;
}

/*
 * [over.over]/2: "a non-template function with type F is selected for the
 * function type FT of the target type if F (after possibly applying the
 * function pointer conversion) is identical to FT"; /3: "the template
 * arguments are deduced ([temp.deduct.funcaddr]) ... if deduction succeeds,
 * the specialization is a function selected"; /5: "if more than one
 * function is selected, any function template specializations in the set
 * are eliminated if the set also contains a function that is not a function
 * template specialization".  Several specializations are chosen among by
 * partial ordering, not analysed.
 */
analysis_result semantic_analysis::select_for_target(
    SyntaxRef at, const function_set &set, qualified_type ft,
    set_selection &how, entity_index &function, uint32_t &template_index,
    owned_array<template_argument> &deduced, const char *&why,
    entity_index member_class)
{
    how = set_selection::none;
    function = 0;
    template_index = 0;
    why = "over.over";
    if (depends_on_template_parameter(ft)) {
        how = set_selection::unknown;
        why = "temp.dep";
        return analysis_result::well_formed;
    }
    bool close = false;
    for (entity_index f : set.functions) {
        const entity &x = entity_table[f];
        if (member_class) {
            if (!x.nonstatic_member)
                continue;
        } else if (x.nonstatic_member) {
            /* /4: a pointer to member */
            how = set_selection::unknown;
            return analysis_result::well_formed;
        }
        /* [over.over]/2: compare after the one-way [conv.fctptr]
           conversion, including when the target is a function reference. */
        bool compatible = type_table[x.type.unqualified()].non_throwing &&
                          differ_in_exceptions(x.type, ft);
        if (x.type.unqualified() == ft.unqualified() || compatible) {
            if (function) {
                how = set_selection::unknown;
                return analysis_result::well_formed;
            }
            function = f;
        } else if (same_parameters(type_table, x.type.unqualified(),
                                   ft.unqualified())) {
            /* A return type or exception specification that differs. */
            close = true;
        }
    }
    if (function) {
        how = set_selection::one;
        return analysis_result::well_formed;
    }
    if (close) {
        how = set_selection::unknown;
        return analysis_result::well_formed;
    }
    for (uint32_t t : set.templates) {
        const entity &te = entity_table[function_templates[t - 1].entity];
        if (member_class) {
            if (!te.nonstatic_member)
                continue;
        } else if (!is_namespace(te.enclosing) && te.nonstatic_member) {
            how = set_selection::unknown;
            return analysis_result::well_formed;
        }
        owned_array<template_argument> arguments;
        bool failed = false;
        qualified_type target = ft;
        const struct type &tf = type_table[ft.unqualified()];
        if (member_class && tf.function_cv && !tf.function_ref) {
            /* [over.over]/3 (a pointer to member function): the template's
               member function has the target's cv-qualifier-seq, kept in
               the entity, not in its type */
            if ((te.member_cv & (cv_const | cv_volatile)) != tf.function_cv)
                continue;
            target = qualified_type::make(
                type_table.function_qualified(ft.unqualified(), 0, 0), ft.cv());
        } else if (member_class && (te.member_cv & (cv_const | cv_volatile)))
            continue;
        VTRY(r, deduce_from_function_type(at, t, target, arguments, failed));
        if (failed)
            continue;
        if (template_index) {
            how = set_selection::unknown;
            why = "temp.func.order";
            return analysis_result::well_formed;
        }
        template_index = t;
        deduced.clear();
        for (const template_argument &a : arguments)
            deduced.append(a);
    }
    if (template_index)
        how = set_selection::one;
    return analysis_result::well_formed;
}

/*
 * [over.ics.scs]: an overload set converts to a parameter of function
 * pointer or function reference type when [over.over] finds one function
 * for it, by an Exact Match; to a parameter of another type (a class's
 * constructors take it by a conversion not analysed), not at all.
 */
semantic_analysis::conversion_sequence
semantic_analysis::overload_set_sequence(const expression_meaning &a,
                                         qualified_type parameter)
{
    conversion_sequence s;
    qualified_type ft;
    const struct type &p = type_table[parameter.unqualified()];
    if (p.kind == type_kind::class_type ||
        depends_on_template_parameter(parameter)) {
        s.kind = conversion_sequence::form::unknown;
        return s;
    }
    if (!function_target(parameter, ft)) {
        /* the pointer or reference to something that is no function (a
           reference to a pointer: [over.over]/1 wants the type of the
           target as the pointer to function) */
        if (p.kind == type_kind::lvalue_reference ||
            p.kind == type_kind::rvalue_reference)
            s.kind = conversion_sequence::form::unknown;
        return s;
    }
    function_set set;
    if (!function_set_at(a.overload_name, set)) {
        s.kind = conversion_sequence::form::unknown;
        return s;
    }
    set_selection how = set_selection::none;
    entity_index function = 0;
    uint32_t template_index = 0;
    owned_array<template_argument> deduced;
    const char *why = nullptr;
    analysis_result r = select_for_target(0, set, ft, how, function,
                                          template_index, deduced, why);
    if (r != analysis_result::well_formed || how == set_selection::unknown) {
        s.kind = conversion_sequence::form::unknown;
        return s;
    }
    if (how == set_selection::none)
        return s;
    s.kind = conversion_sequence::form::standard;
    s.target = parameter;
    if (function && differ_in_exceptions(entity_table[function].type, ft))
        s.identity = false;
    if (p.kind != type_kind::pointer) {
        s.reference = true;
        s.function_lvalue = true;
        s.rvalue_reference = p.kind == type_kind::rvalue_reference;
    }
    return s;
}

/*
 * [over.over]/1: "A use of a function name without arguments is resolved
 * to a function, a pointer to function, or a pointer to member function
 * for a specific function that is chosen from a set of selected functions
 * determined based on the target type required in the context": the
 * expression e initializing an object or reference of type target, its
 * overload set (if it names one, with or without &) narrowed to the
 * function select_for_target chooses for the target's function type; any
 * other expression analysed as it is.
 */
analysis_result semantic_analysis::target_expression(SyntaxElement e,
                                                     qualified_type target,
                                                     expression_meaning &m)
{
    qualified_type ft;
    entity_index member_class = 0;
    if (!function_target(target, ft, &member_class))
        return expression(e, m);
    function_set set;
    if (!named_function_set(e, set))
        return expression(e, m);
    /* whether &f or f fits the target (&f initializes no reference) is
       the initialization's rules, of the function chosen */
    SyntaxRef at = current_declaration;
    if (without_parentheses(e).is_node())
        at = without_parentheses(e).index();
    set_selection how = set_selection::none;
    entity_index chosen = 0;
    uint32_t template_index = 0;
    owned_array<template_argument> deduced;
    const char *why = nullptr;
    VTRY(r, select_for_target(at, set, ft, how, chosen, template_index, deduced,
                              why, member_class));
    if (how == set_selection::unknown)
        return not_supported(current_declaration, why);
    if (how == set_selection::none)
        return ill_formed_at_token(set.at,
                                   "no function of the overload set has the "
                                   "type of the target",
                                   "over.over");
    if (template_index) {
        VTRY_SET(r,
                 instantiate(at, function_templates[template_index - 1].entity,
                             deduced.data(),
                             static_cast<uint32_t>(deduced.size()), chosen));
    }
    /* the name is analysed as the function chosen (id_expression) */
    overload_target_token = set.at;
    overload_target_choice = chosen;
    size_t uses_before = name_uses.size();
    r = expression(e, m);
    overload_target_token = 0;
    overload_target_choice = 0;
    if (template_index)
        name_uses.resize(uses_before);
    return r;
}

} // namespace cplusplus
