/*
 * variable-template.cpp: Variable templates ([temp.pre]/1, [temp.inst]/7): a
 * variable template's declaration, its specializations and their
 * instantiation.
 * Split from declared-templates.cpp, which has [temp.pre]'s declarations of
 * templates and what the kinds share.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"
#include "declared-templates.h"

namespace cplusplus
{

uint32_t semantic_analysis::variable_template_of(entity_index e) const
{
    return template_of_form(e, &function_template::is_variable);
}

/*
 * [temp.pre]/1: "A template defines a family of classes, functions, or
 * variables"; /5: "the init-declarator-list in the declaration shall
 * contain at most one declarator".  One
 * at namespace scope, not declared before, whose init-declarator has an
 * initializer: its decl-specifier-seq and declarator are analysed with the
 * template-parameters' types, the initializer per specialization
 * (instantiate_variable).
 */
analysis_result
semantic_analysis::variable_template_definition(SyntaxRef n,
                                                SyntaxRef declaration)
{
    entity_index outer = current_scope;
    owned_array<SyntaxRef> parameters;
    VTRY(r, template_head_parameters(n, parameters, true));
    uint32_t skip = attributes_without_effect_before(declaration);
    auto item = declaration[skip + 1];
    if (item.is(syntax_kind::member_declarator_list) && item.size() == 1)
        item = item[0];
    bool has_init = false, has_definition = false;
    SyntaxElement decl_part = item;
    if (item.is(syntax_kind::init_declarator, syntax_kind::member_declarator)) {
        if (item.size() != 2 || item[1].is(syntax_kind::requires_clause))
            return not_supported(n, "temp.pre");
        decl_part = item[0];
        has_init = true;
    } else if (declaration.is(syntax_kind::simple_declaration)) {
        /* (no initializer: the declaration is a definition) */
        has_definition = true;
    } else if (!declaration.is(syntax_kind::member_declaration)) {
        return not_supported(n, "temp.pre");
    }
    /* (the name is not known before the declarator: the template-parameters
       are checked against it below) */
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY_SET(r, template_parameter_scope(n, parameters, outer, 0, index, scope,
                                         first));
    if (default_missing_after_one(parameters))
        return not_supported(n, "temp.param");
    entity_index saved_scope = current_scope;
    uint32_t saved_cv = function_cv;
    current_scope = scope;
    scoped_value<entity_index> member_scope(template_member_scope, scope);
    specified s;
    declarator_meaning m;
    /* [dcl.spec.auto.general]/4: a placeholder in a variable template's
       declaration with an initializer; deduced in each specialization */
    placeholder_allowed = has_init;
    r = decl_specifier_seq(declaration[skip], s, false);
    placeholder_allowed = false;
    qualified_type type = s.type;
    if (r == analysis_result::well_formed) {
        function_cv = 0;
        r = declarator(declaration, decl_part, type, m);
        if (r == analysis_result::well_formed)
            r = misplaced_function_qualifiers(declaration, type, false);
    }
    current_scope = saved_scope;
    function_cv = saved_cv;
    if (r != analysis_result::well_formed)
        return r;
    if (!m.has_id || m.qualified_namespace || !s.has_type || s.typedef_ ||
        s.extern_ || s.declares_enumeration || s.gnu_flags || s.aligned ||
        m.operator_function || m.literal_operator ||
        type_table[type.unqualified()].kind == type_kind::function ||
        type_table[type.unqualified()].kind == type_kind::none)
        return not_supported(n, "temp.pre");
    for (SyntaxElement k : declaration[skip].elements()) {
        if (k.is(syntax_kind::class_specifier,
                 syntax_kind::elaborated_type_specifier,
                 syntax_kind::enum_specifier))
            return not_supported(n, "temp.pre");
    }
    uint32_t name = tokens.identifier(m.id);
    /* [temp.mem]/1: out-of-class definition of a static data member template */
    if (m.qualifier) {
        if (!has_init ||
            entity_table[m.qualifier].kind != entity_kind::class_type)
            return not_supported(n, "temp.pre");
        entity_index prior = entity_table.find(m.qualifier, name);
        uint32_t vt = variable_template_of(prior);
        if (!vt || function_templates[vt - 1].definition ||
            function_templates[vt - 1].parameter_count != parameters.size() ||
            !same_template_parameters(
                first, function_templates[vt - 1].first_parameter,
                function_templates[vt - 1].parameter_count))
            return not_supported(n, "temp.pre");
        function_template &record = function_templates[vt - 1];
        VTRY(merged, merge_template_defaults(n, record.first_parameter, first,
                                             record.parameter_count, false));
        record.declaration = n;
        record.definition = declaration;
        record.declarator = declaration;
        record.first_parameter = first;
        entity_table[record.entity].defined = true;
        return analysis_result::well_formed;
    }
    if (entity_table.find(outer, name))
        return not_supported(n, "temp.pre");
    /* [temp.local]/6: "A template-parameter shall not have the same name
       as the template name." (clang's error) */
    for (uint32_t i = 0; i < parameters.size(); i++)
        if (entity_table[template_parameter_entities[first + i]].name == name)
            return not_supported(n, "temp.local");
    function_template record = new_template(
        name, outer, n, index, first, static_cast<uint32_t>(parameters.size()));
    has_definition = has_definition || has_init;
    entity_table[record.entity].defined = has_definition;
    entity_table[record.entity].constexpr_ = s.constexpr_;
    if (has_definition)
        record.definition = declaration;
    record.declarator = declaration;
    record.is_variable = true;
    hold_template_name(record, name, outer);
    return analysis_result::well_formed;
}

/*
 * [temp.inst]/7: "Unless a variable template specialization is a declared
 * specialization, the variable template specialization is implicitly
 * instantiated when it is referenced in a context that requires a
 * variable definition to exist or if the existence of the definition
 * affects the semantics of the program."  Here at any use: the template's
 * simple-declaration analysed again where each template-parameter's name
 * is its argument, the variable made in the template's namespace without
 * binding its name (declare_variable_specialization); inline, [impl.clang] as
 * clang makes an implicit instantiation (linkonce_odr).
 */
analysis_result semantic_analysis::variable_template_id(SyntaxRef n,
                                                        expression_meaning &m)
{
    lookup_result x;
    entity_index named = 0;
    if (!template_id_name(n, x, named))
        return not_supported(n, "temp.names");
    if (uint32_t ct = concept_of(named))
        if (!x.ambiguous)
            return concept_id(n, ct, m);
    uint32_t vt = variable_template_of(named);
    if (!vt && !x.ambiguous)
        if (uint32_t ft = function_template_of(x.unknown_entity))
            return function_template_id_value(n, ft, m);
    if (!vt || x.ambiguous)
        return not_supported(n, "temp.names");
    return variable_template_id_of(n, vt, m);
}

analysis_result
semantic_analysis::variable_template_id_of(SyntaxRef n, uint32_t vt,
                                           expression_meaning &m)
{
    owned_array<template_argument> arguments;
    VTRY(r, template_id_arguments(n, vt, arguments));
    if (dependent_arguments(arguments))
        return not_supported(n, "temp.dep");
    entity_index e = 0;
    VTRY_SET(r,
             instantiate_variable(n, vt, arguments.data(),
                                  static_cast<uint32_t>(arguments.size()), e));
    size_t uses_before = name_uses.size();
    r = named_entity(n[0].token(), e, m);
    /* (clang's lookup of the name finds the template, no claim here) */
    name_uses.resize(uses_before);
    return r;
}

analysis_result
semantic_analysis::instantiate_variable(SyntaxRef at, uint32_t vt,
                                        const template_argument *arguments,
                                        uint32_t count, entity_index &e)
{
    e = 0;
    const function_template ft = function_templates[vt - 1];
    if (count != ft.parameter_count)
        return not_supported(at, "temp.inst");
    if (uint32_t prior = find_instantiation(ft.entity, arguments, count)) {
        const instantiation_record &x = instantiations[prior - 1];
        /* (one whose initializer uses itself, or that failed) */
        if (!x.specialization || !x.defined)
            return not_supported(at, "temp.inst");
        e = x.specialization;
        return analysis_result::well_formed;
    }
    if (instantiation_depth >= 64)
        return not_supported(at, "temp.inst");
    /* [temp.class.spec.match] for variable templates: a partial
       specialization's declaration, its parameters the deduced arguments */
    uint32_t pattern = 0;
    owned_array<template_argument> deduced;
    VTRY(matched,
         matching_specialization(at, vt, arguments, count, pattern, deduced));
    uint32_t index = record_instantiation(ft, arguments, count);
    function_template definition = ft;
    if (pattern) {
        definition = function_templates[pattern - 1];
        entity_index bound = entity_table.make(
            entity_kind::block_scope, 0, entity_table[ft.entity].enclosing);
        for (uint32_t i = 0; i < definition.parameter_count; i++)
            bind_template_argument(
                bound,
                template_parameter_entities[definition.first_parameter + i],
                deduced[i]);
        instantiations[index].scope = bound;
        instantiations[index].pattern = pattern;
    }
    entity_index scope = instantiations[index].scope;
    instantiation_context context;
    enter_instantiation(context, scope, definition.declaration);
    template_member_scope = scope;
    expanding = false;
    instantiating = 0;
    variable_instantiating = index + 1;
    instantiations[index].defining = true;
    /* (a partial specialization's declarator-id: its template-id) */
    declarator_template_id = pattern != 0;
    analysis_result r = simple_declaration(definition.declarator);
    declarator_template_id = false;
    instantiations[index].defining = false;
    leave_instantiation(context);
    /* an argument that makes the declaration ill-formed: [impl.clang] clang
       reports it at the template, with a note at the use */
    if (r != analysis_result::well_formed ||
        !instantiations[index].specialization)
        return not_supported(at, "temp.inst");
    instantiations[index].defined = true;
    e = instantiations[index].specialization;
    return analysis_result::well_formed;
}

} // namespace cplusplus
