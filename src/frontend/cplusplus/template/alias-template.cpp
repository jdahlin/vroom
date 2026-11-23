/*
 * alias-template.cpp: [temp.alias] Alias templates: an alias template's
 * declaration and the type a specialization of it designates.
 * Split from declared-templates.cpp, which has [temp.pre]'s declarations of
 * templates and what the kinds share.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"
#include "declared-templates.h"

namespace cplusplus
{

/*
 * [temp.alias]/1: "A template-declaration in which the declaration is an
 * alias-declaration declares the identifier to be an alias template.  An
 * alias template is a name for a family of types."  One at namespace
 * scope, not declared before: its defining-type-id is analysed with the
 * template-parameters' types, then per template-id (alias_template_type).
 */
analysis_result semantic_analysis::alias_template_definition(SyntaxRef n,
                                                             SyntaxRef alias)
{
    entity_index outer = current_scope;
    owned_array<SyntaxRef> parameters;
    VTRY(r, template_head_parameters(n, parameters, true, true, false, true));
    uint32_t name = tokens.identifier(alias.first_token() + 1);
    if (entity_index prior = entity_table.find(outer, name)) {
        /* a redeclaration of the same tokens (libc++'s __swap_result_t in
           two headers), [ext.clang] as clang accepts it; [temp.alias] has no
           rule for one that differs */
        uint32_t same = alias_template_of(prior);
        if (!same || !same_tokens(function_templates[same - 1].declaration, n))
            return not_supported(n, "temp.alias");
        entity_table.add_declaration(function_templates[same - 1].entity, n);
        return analysis_result::well_formed;
    }
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY_SET(r, template_parameter_scope(n, parameters, outer, name, index,
                                         scope, first));
    /* [temp.param]/14's rule is a class template's: a default after which
       one is missing is left to clang */
    if (default_missing_after_one(parameters))
        return not_supported(n, "temp.param");
    qualified_type pattern;
    entity_index saved_scope = current_scope;
    current_scope = scope;
    size_t unsupported_before = unsupported.size();
    r = alias_type(alias, pattern);
    current_scope = saved_scope;
    /* a type analysis cannot follow with the parameters unbound (T...
       where the template named takes a parameter before its pack,
       typename Q<T>::template X<U> of a dependent Q): it is analysed with
       each use's arguments (alias_template_type), as it is anyway, and
       judged there */
    if (r == analysis_result::unsupported) {
        unsupported.resize(unsupported_before);
        r = analysis_result::well_formed;
    }
    if (r != analysis_result::well_formed)
        return r;
    if (index != function_templates.size()) {
        index = static_cast<uint32_t>(function_templates.size());
        for (size_t i = 0; i < parameters.size(); i++) {
            entity_index e = template_parameter_entities[first + i];
            if (entity_table[e].kind != entity_kind::template_entity)
                entity_table[e].instantiation = index + 1;
        }
    }
    function_template record = new_template(
        name, outer, n, index, first, static_cast<uint32_t>(parameters.size()));
    entity_table[record.entity].defined = true;
    record.definition = alias;
    record.is_alias = true;
    hold_template_name(record, name, outer);
    return analysis_result::well_formed;
}

/*
 * [temp.alias]/2: "When a template-id refers to the specialization of an
 * alias template, it is equivalent to the associated type obtained by
 * substitution of its template-arguments for the template-parameters in
 * the defining-type-id of the alias template."  The defining-type-id
 * analysed again where each template-parameter's name is its argument
 * (a dependent argument's type a template's own, which deduction then
 * sees through).
 */
analysis_result semantic_analysis::alias_template_type(SyntaxRef id,
                                                       uint32_t at,
                                                       type_index &type,
                                                       uint32_t *cv)
{
    owned_array<template_argument> arguments;
    VTRY(r, template_id_arguments(id, at, arguments));
    return alias_template_specialization(id, at, arguments, type, cv);
}

/* the type alias template function_templates[at - 1] denotes with
   arguments (one per parameter, a pack's as one) */
analysis_result semantic_analysis::alias_template_specialization(
    SyntaxRef id, uint32_t at, const owned_array<template_argument> &arguments,
    type_index &type, uint32_t *cv)
{
    const function_template ft = function_templates[at - 1];
    analysis_result r = analysis_result::well_formed;
    /* an enclosing template's own pack expanded into the alias's pack
       (Numbers... unbound): its elements are not known.  Forwarded whole
       to a pack parameter (libc++'s __invoke_result_t<_Args...>), the
       parameter's name is bound to the enclosing pack itself, so that the
       alias denotes what its type-id with that pack written there would
       ([temp.alias]/2); anything else (__invoke_result_t<_Fn, _Args...>)
       is the dependent template-id itself ([temp.dep.type]/9), analysed
       again with the elements when the enclosing template is
       instantiated ([temp.variadic]/5) */
    owned_array<entity_index> forwarded;
    for (uint32_t i = 0; i < ft.parameter_count; i++) {
        const template_argument &a = arguments[i];
        entity_index whole = 0;
        if (a.kind == template_argument::kind_t::pack)
            for (uint32_t k = 0; k < a.count; k++) {
                const template_argument &x = pack_arguments[a.value + k];
                if (x.count != UINT32_MAX)
                    continue;
                entity_index parameter =
                    template_parameter_entities[ft.first_parameter + i];
                bool same_non_type =
                    entity_table[x.template_entity].kind == entity_kind::none &&
                    entity_table[parameter].kind == entity_kind::none &&
                    (entity_table[x.template_entity].type ==
                         entity_table[parameter].type ||
                     contains_placeholder(entity_table[parameter].type));
                if (a.count != 1 ||
                    !template_parameter_is_pack(ft.first_parameter + i) ||
                    (entity_table[x.template_entity].kind !=
                         entity_kind::typedef_name &&
                     !same_non_type)) {
                    type = dependent_specialization(ft.entity, arguments);
                    return analysis_result::well_formed;
                }
                whole = x.template_entity;
            }
        forwarded.append(whole);
    }
    /* [temp.alias]/2: "when a template-id refers to the specialization of
       an alias template, it is equivalent to the associated type", which
       [temp.names]/7 requires to satisfy the template's associated
       constraints ([temp.constr.decl]/3); of dependent arguments, they
       cannot be checked yet, and the type is the defining-type-id with the
       arguments substituted, itself dependent (clang mangles it so) */
    if (!dependent_arguments(arguments)) {
        bool satisfied = true;
        VTRY_SET(r, template_constraints_satisfied(at, arguments, satisfied));
        /* [impl.clang] clang reports the unsatisfied constraint at the use;
           while a candidate is substituted, a deduction failure */
        if (!satisfied && substituting)
            return analysis_result::ill_formed;
        if (!satisfied)
            return not_supported(id, "temp.names");
    }
    entity_index scope = entity_table.make(entity_kind::block_scope, 0,
                                           entity_table[ft.entity].enclosing);
    for (uint32_t i = 0; i < ft.parameter_count; i++) {
        entity_index parameter =
            template_parameter_entities[ft.first_parameter + i];
        if (forwarded[i]) {
            entity_table.bind(scope, entity_table[parameter].name,
                              forwarded[i]);
            continue;
        }
        bind_template_argument(scope, parameter, arguments[i]);
    }
    entity_index saved_scope = current_scope;
    token_index saved_horizon = lookup_horizon;
    bool saved_expanding = expanding;
    current_scope = scope;
    lookup_horizon = ft.declaration.first_token();
    expanding = false;
    qualified_type t;
    size_t entities_before = entity_table.size();
    r = alias_type(ft.definition, t);
    current_scope = saved_scope;
    lookup_horizon = saved_horizon;
    expanding = saved_expanding;
    /* [impl.clang] a closure type created by substituting an alias template
       belongs to the function that substituted it; a closure type the
       alias only names (an argument, as __decay_t<_Poll>) keeps its
       scope */
    if (r == analysis_result::well_formed &&
        type_table[t.unqualified()].kind == type_kind::class_type) {
        entity_index c = type_table[t.unqualified()].entity;
        if (entity_table[c].lambda && c >= entities_before) {
            entity_index fn = saved_scope;
            while (fn && entity_table[fn].kind != entity_kind::function)
                fn = entity_table[fn].enclosing;
            if (fn)
                entity_table[c].enclosing = saved_scope;
        }
    }
    /* an argument that makes the type ill-formed: [impl.clang] clang reports it
       at the alias template, with a note at the use; while a candidate is
       substituted, [temp.alias]/3's deduction failure */
    if (r == analysis_result::ill_formed && substituting)
        return r;
    if (r == analysis_result::ill_formed)
        return not_supported(id, "temp.alias");
    if (r != analysis_result::well_formed)
        return r;
    /* a cv-qualified type (alias<const T>) to a caller that takes its cv;
       the others refuse it rather than drop the cv */
    if (t.cv() && !cv)
        return not_supported(id, "temp.alias");
    /* [temp.alias]/4 (CWG 1558): "when a template-id refers to the
       specialization of an alias template ... any substitution failure in
       the template arguments of the template-id is a substitution failure
       of the instantiation": dependent arguments the type does not keep
       (void_t<typename T::type> is void) are substituted again with each
       instantiation, which a non-dependent type does not record; a
       partial specialization's are (substitute_specialization) */
    if (!partial_arguments &&
        !depends_on_template_parameter(
            qualified_type::make(t.unqualified(), cv_none))) {
        if (dependent_arguments(arguments))
            return not_supported(id, "temp.alias");
    }
    type = t.unqualified();
    if (cv)
        *cv = t.cv();
    return analysis_result::well_formed;
}

} // namespace cplusplus
