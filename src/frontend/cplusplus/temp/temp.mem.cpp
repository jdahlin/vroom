/*
 * temp.mem.cpp: [temp.mem] Member templates.
 *
 * [temp.mem]/1: "A template can be declared within a class or class
 * template; such a template is called a member template.  A member
 * template can be defined within or outside its class definition or class
 * template definition."  Here a member function template defined in a
 * class of namespace scope: a function_templates record whose entity's
 * scope is the class, its name held in the class (hold_template_name);
 * a constructor template's name is held by the class's
 * constructor_templates, not bound, and its specializations are
 * candidates of a construction (construct) and of a conversion to the
 * class (class_sequence).  Its body is analysed per specialization only
 * (instantiate, instantiate_definition), the specialization a member
 * function of the class (declare_specialization).  A call x.f(args),
 * x->f(args) or, through operator(), x(args) deduces its template
 * arguments from the arguments ([temp.deduct.call]); any other use of the
 * name (unqualified in a member function, X::f, &X::f, an explicit
 * template argument list) is not analysed.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * template-head function-definition in the member-specification of class
 * c: the template declared, its definition kept for instantiation.
 */
analysis_result semantic_analysis::member_function_template(
    entity_index c, SyntaxRef n, SyntaxRef definition, uint8_t access)
{
    owned_array<SyntaxRef> parameters;
    /* (the template-head's rules are a namespace-scope template's) */
    entity_index saved_class = defining_class;
    defining_class = 0;
    analysis_result head =
        template_head_parameters(n, parameters, false, true, false, true);
    defining_class = saved_class;
    if (head != analysis_result::well_formed)
        return head;
    auto d = definition;
    uint32_t lead = leading_gnu_attributes(definition);
    /* a constructor template's decl-specifier-seq is optional
       ([class.ctor]/1) */
    uint32_t specifiers =
        d.size() > lead && d[lead].is(syntax_kind::decl_specifier_seq);
    bool constructor = declares_special_member(c, d[lead + specifiers]);
    /* [dcl.decl]/4: a trailing requires-clause (trailing_constraint),
       where the template-head has none */
    uint32_t trailing = static_cast<bool>(trailing_constraint(n));
    if (trailing && head_constraint(n))
        return not_supported(n, "temp.constr");
    if (d.size() != lead + 2 + specifiers + trailing ||
        (!specifiers && !constructor) || d[lead + specifiers].is_token() ||
        !d[lead + specifiers + 1 + trailing].is(syntax_kind::function_body))
        return not_supported(n, "temp.mem");
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    analysis_result declared =
        template_parameter_scope(n, parameters, c, 0, index, scope, first);
    if (declared != analysis_result::well_formed)
        return declared;
    for (uint32_t i = 0; i < parameters.size(); i++)
        if (template_parameter_is_pack(first + i))
            return not_supported(n, "temp.variadic");
    /* the declarator, in the template-parameters' scope (whose enclosing
       scope is the class, [basic.scope.class]) */
    specified s;
    declarator_meaning m;
    qualified_type type;
    analysis_result r =
        function_template_declarator(definition, lead, c, scope, s, m, type);
    if (r != analysis_result::well_formed)
        return r;
    /* [temp.mem]/3: "A member function template shall not be virtual." */
    if (s.virtual_)
        return ill_formed_at_token(m.id,
                                   "a member function template shall not be "
                                   "virtual",
                                   "temp.mem");
    if (!m.has_id || m.qualifier || m.qualified_namespace || m.destructor ||
        m.conversion_function || m.literal_operator ||
        (s.explicit_ && !constructor) ||
        m.allocation != allocation_function::none ||
        type_table[type.unqualified()].kind != type_kind::function)
        return not_supported(n, "temp.mem");
    if (s.static_ && function_cv)
        return ill_formed_at_token(m.id,
                                   "a static member function shall not be "
                                   "declared const, volatile, or const "
                                   "volatile",
                                   "class.static.mfct");
    uint32_t gnu_flags = 0;
    token_index abi_tag = 0;
    r = function_template_attributes(n, s, m, 0, "temp.mem", gnu_flags,
                                     abi_tag);
    if (r != analysis_result::well_formed)
        return r;
    uint32_t name = tokens.identifier(m.id);
    /* [temp.over]/1: other member function templates of the name are
       overloads; a non-template member of the name too ([temp.mem]/2),
       which a call would weigh against them: not analysed */
    entity_index prior = 0;
    if (!constructor)
        prior = entity_table.find(c, name);
    if (prior && (entity_table[prior].enclosing != c ||
                  !member_function_template_of(prior)))
        return not_supported(n, "temp.mem");
    function_template record = new_template(
        name, c, n, index, first, static_cast<uint32_t>(parameters.size()));
    record.gnu_flags = gnu_flags;
    record.abi_tag = abi_tag;
    record.definition = definition;
    record.declarator = definition;
    entity_index t = record.entity;
    entity_table[t].type = type;
    entity_table[t].defined = true;
    entity_table[t].linkage_of_name = entity_table[c].linkage_of_name;
    entity_table[t].nonstatic_member = !s.static_;
    entity_table[t].member_cv = function_cv;
    entity_table[t].access = access;
    /* [class.mfct]/1: defined in its class, inline */
    entity_table[t].inline_ = true;
    entity_table[t].constexpr_ = s.constexpr_;
    entity_table[t].immediate = s.consteval_;
    entity_table[t].operator_function = m.operator_function;
    entity_table[t].implemented = m.implemented;
    if (constructor) {
        /* [class.ctor]/2: a constructor has no name to find; the class
           keeps its templates' (construct, class_sequence) */
        entity_table[t].special = special_member::constructor;
        entity_table[t].explicit_ = s.explicit_;
        prior = entity_table[c].constructor_templates;
        if (!prior) {
            prior = entity_table.make(entity_kind::none, name, c);
            entity_table[prior].held_by_template = true;
            entity_table[c].constructor_templates = prior;
        }
    }
    if (prior) {
        record.name = prior;
        function_templates.append(record);
    } else {
        hold_template_name(record, name, c);
    }
    return analysis_result::well_formed;
}

/*
 * [temp.over]/1: the specializations of the member function templates
 * named held deduced from the arguments (not the object's), appended to
 * candidates as member candidates ([over.match.funcs]/2).
 */
analysis_result semantic_analysis::member_template_candidates(
    SyntaxRef n, entity_index held, const expression_meaning *arguments,
    uint32_t count, owned_array<overload_candidate> &candidates)
{
    owned_array<uint32_t> group;
    function_template_overloads(held, group);
    for (uint32_t g : group)
        if (!entity_table[function_templates[g - 1].entity].nonstatic_member)
            /* a static member function template: no implicit object
               parameter to match ([over.match.funcs]/4) */
            return not_supported(n, "temp.mem");
    size_t first = candidates.size();
    analysis_result r =
        template_candidates(n, group, arguments, count, candidates);
    for (size_t i = first; i < candidates.size(); i++)
        candidates[i].member = true;
    return r;
}

/*
 * [over.match.ctor]/1, [temp.over]/1: the specializations of class c's
 * constructor templates deduced from the arguments, appended to
 * candidates; for a copy-initialization only the converting ones
 * ([class.conv.ctor]/1: not explicit).
 */
analysis_result semantic_analysis::constructor_template_candidates(
    SyntaxRef n, entity_index c, const expression_meaning *arguments,
    uint32_t count, bool copy, owned_array<overload_candidate> &candidates,
    owned_array<uint32_t> *first_arguments, owned_array<template_argument> *all)
{
    owned_array<uint32_t> group;
    function_template_overloads(entity_table[c].constructor_templates, group);
    if (copy) {
        size_t kept = 0;
        for (uint32_t g : group)
            if (!entity_table[function_templates[g - 1].entity].explicit_)
                group[kept++] = g;
        group.resize(kept);
    }
    owned_array<template_argument> deduced;
    if (!all)
        all = &deduced;
    owned_array<uint32_t> firsts;
    if (!first_arguments)
        first_arguments = &firsts;
    return template_candidates(n, group, arguments, count, candidates,
                               first_arguments, all);
}

/*
 * [over.call.func]/2: a call whose postfix-expression is a class member
 * access naming member function templates (held: their name), of object
 * (a glvalue of the class or of a class derived from it).  [temp.over]/1:
 * each template's specialization deduced from the arguments
 * ([temp.deduct.call]) is a candidate, with the implied object argument
 * ([over.match.funcs]/2); the best one's definition is instantiated
 * ([temp.inst]/4) and called.
 */
analysis_result semantic_analysis::member_template_call(
    SyntaxRef n, entity_index held, const expression_meaning &object,
    const owned_array<SyntaxElement> &arguments,
    owned_array<expression_meaning> &early, expression_meaning &m)
{
    if (early.empty())
        for (SyntaxElement a : arguments) {
            token_index set_at = 0;
            if (a.is(syntax_kind::braced_init_list) ||
                named_overload_set(a, set_at))
                return not_supported(n, "temp.deduct.call");
            expression_meaning e;
            analysis_result r = expression(a, e);
            if (r != analysis_result::well_formed)
                return r;
            early.append(e);
        }
    for (const expression_meaning &e : early)
        if (e.entity && entity_table[e.entity].kind == entity_kind::function)
            return not_supported(n, "temp.deduct.call");
    owned_array<overload_candidate> candidates;
    analysis_result r = member_template_candidates(
        n, held, early.data(), static_cast<uint32_t>(early.size()), candidates);
    if (r != analysis_result::well_formed)
        return r;
    if (candidates.empty())
        return not_supported(n, "temp.deduct");
    owned_array<expression_meaning> all;
    all.append(object);
    for (const expression_meaning &e : early)
        all.append(e);
    size_t chosen = 0;
    bool none_viable = false;
    r = best_viable_function(n, candidates, nullptr, all, chosen, &none_viable);
    if (r != analysis_result::well_formed)
        return r;
    if (none_viable)
        return not_supported(n, "over.match.viable");
    const overload_candidate &k = candidates[chosen];
    owned_array<template_argument> deduced;
    r = deduce_from_call(n, k.template_index, early.data(),
                         static_cast<uint32_t>(early.size()), deduced);
    entity_index f = 0;
    if (r == analysis_result::well_formed)
        r = instantiate(n, function_templates[k.template_index - 1].entity,
                        deduced.data(), static_cast<uint32_t>(deduced.size()),
                        f);
    if (r != analysis_result::well_formed)
        return r;
    return bound_member_call(n, f, object, arguments, early, m);
}

/* member function f called on object (of its class or a class derived
   from it) with the arguments: [expr.call]/2's class member access */
analysis_result semantic_analysis::bound_member_call(
    SyntaxRef n, entity_index f, const expression_meaning &object,
    const owned_array<SyntaxElement> &arguments,
    owned_array<expression_meaning> &early, expression_meaning &m)
{
    token_index at = n.first_token();
    expression_meaning e = object;
    entity_index object_class = type_table[e.type.unqualified()].entity;
    const entity x = entity_table[f];
    if (!accessible(f, object_class))
        /* [class.access]/4: "access control is applied to the function
           selected by overload resolution" */
        return ill_formed(n,
                          "a private or protected member's name can be used "
                          "only by members and friends of the class in which "
                          "it is declared",
                          "class.access");
    if (x.enclosing != object_class) {
        analysis_result r = to_base(at, x.enclosing, e);
        if (r != analysis_result::well_formed)
            return r;
    }
    if (e.category == value_category::prvalue &&
        entity_table[x.enclosing].destructor)
        /* the temporary's destruction ([class.temporary]/4) */
        return not_supported(n, "class.temporary");
    if (aggregate_prvalue(e))
        return not_supported(n, "class.temporary");
    analysis_result r = deleted_use(at, f);
    if (r != analysis_result::well_formed)
        return r;
    expression_meaning callee;
    callee.type = x.type;
    callee.category = value_category::lvalue;
    callee.evaluation = constness::constant;
    callee.entity = f;
    expression_meaning address;
    address.type = qualified_type::make(type_table.pointer_to(e.type), cv_none);
    address.category = value_category::prvalue;
    address.evaluation = constness::not_constant;
    address.node = expression_node(semantic_kind::address, address, at, e.node);
    return call_with(n, callee, x.type, address.node, f, arguments, early, m);
}

} // namespace cplusplus
