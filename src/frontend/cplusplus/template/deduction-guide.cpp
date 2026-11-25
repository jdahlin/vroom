/*
 * deduction-guide.cpp: [temp.deduct.guide] Deduction guides: a deduction-
 * guide's declaration and the class template argument deduction candidates it
 * makes ([over.match.class.deduct]).
 * Split from declared-templates.cpp, which has [temp.pre]'s declarations of
 * templates and what the kinds share.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"
#include "declared-templates.h"

namespace cplusplus
{

/* the parts of a deduction-guide: its template-name (the token before the
   parameters), its parameters-and-qualifiers and its simple-template-id */
struct guide_parts {
    token_index name = 0;
    SyntaxRef parameters = 0, id = 0;
};

static guide_parts guide_parts_of(SyntaxRef guide)
{
    guide_parts p;
    for (uint32_t i = 0; i < guide.size(); i++) {
        SyntaxElement c = guide[i];
        if (c.is(syntax_kind::parameters_and_qualifiers)) {
            p.parameters = c.index();
            p.name = guide[i - 1].token();
        } else if (c.is(syntax_kind::simple_template_id)) {
            p.id = c.index();
        }
    }
    return p;
}

/* [temp.deduct.guide]/1: "Deduction guides are used when a template-name
   appears as a type specifier for a deduced class type" */
analysis_result semantic_analysis::deduction_guide(DeductionGuide n)
{
    guide_parts g = guide_parts_of(n);
    token_index name = g.name;
    SyntaxRef id = g.id;
    /* [temp.deduct.guide]/3: "The template-name shall be the same
       identifier as the template-name of the simple-template-id." */
    if (!id || tokens.identifier(id.first_token()) != tokens.identifier(name))
        return ill_formed_at_token(name,
                                   "the template-name shall be the same "
                                   "identifier as the template-name of the "
                                   "simple-template-id",
                                   "temp.deduct.guide");
    return analysis_result::well_formed;
}

/*
 * [temp.deduct.guide]/1: "Deduction guides are not found by name lookup.
 * Instead, when overload resolution is performed to determine the
 * deduced type of a placeholder for a deduced class type
 * ([over.match.class.deduct]), all reachable deduction guides declared
 * for the class template are considered."  A guide with a template-head
 * is a function template of the guide's parameters returning the
 * simple-template-id's class, kept in function_templates with the class
 * template it is for and bound to no name; deduced_class's overload
 * resolution is among them.  A guide without a template-head, and the
 * implicit guides from the class template's constructors
 * ([over.match.class.deduct]/1.1), are not candidates yet.
 */
analysis_result semantic_analysis::deduction_guide(TemplateDeclaration t,
                                                   DeductionGuide n)
{
    VTRY(named, deduction_guide(n));
    /* a guide whose form is not analysed (a pack, a constraint) is no
       candidate, as every guide was before: no refusal of its own, which
       would refuse the headers that only declare it */
    size_t unsupported_before = unsupported.size();
    entity_index outer = current_scope;
    analysis_result r = deduction_guide_candidate(t, n);
    current_scope = outer;
    if (r == analysis_result::unsupported) {
        unsupported.resize(unsupported_before);
        return analysis_result::well_formed;
    }
    return r;
}

analysis_result
semantic_analysis::deduction_guide_candidate(TemplateDeclaration t,
                                             DeductionGuide n)
{
    entity_index outer = current_scope;
    lookup_result x = lookup_unqualified(
        tokens.identifier(guide_parts_of(n).name), lookup_filter::any);
    uint32_t ct = class_template_of(x.unknown_entity);
    /* a template-name of a class template, which the guide's simple-template-id
       names too ([temp.deduct.guide]/3) */
    if (!ct || function_templates[ct - 1].is_alias)
        return not_supported_because(
            t, "temp.deduct.guide",
            "a deduction guide for a name that is no class template");
    owned_array<SyntaxRef> parameters;
    VTRY(head, template_head_parameters(t, parameters, true, true, true, true));
    if (trailing_constraint(t) || head_constraint(t))
        return not_supported_because(t, "temp.constr",
                                     "a deduction guide with a constraint");
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY(declared, template_parameter_scope(t, parameters, outer, 0, index,
                                            scope, first));
    qualified_type type;
    current_scope = scope;
    analysis_result r = deduction_guide_type(n, type);
    current_scope = outer;
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
    function_template record =
        new_template(tokens.identifier(guide_parts_of(n).name), outer, t, index,
                     first, static_cast<uint32_t>(parameters.size()));
    record.declarator = n;
    record.guide_of = ct;
    entity_table[record.entity].type = type;
    if (!is_namespace(outer))
        entity_table[record.entity].inline_ = true;
    function_templates.append(record);
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::deduction_guide_type(SyntaxRef guide,
                                                        qualified_type &type)
{
    guide_parts g = guide_parts_of(guide);
    type_index returned = 0;
    VTRY(r, template_type(g.id, 0, false, returned));
    type = qualified_type::make(returned, cv_none);
    function_cv = 0;
    function_parameters.clear();
    return function(g.parameters, type);
}

/*
 * [over.match.class.deduct]/1.1: "For each constructor of the primary class
 * template designated by the template-name, if the template is defined, a
 * function template with the following properties is a guide: The template
 * parameters are the template parameters of C followed by the template
 * parameters (including default template arguments) of the constructor, if
 * any.  The types of the function parameters are those of the constructor.
 * The return type is the simple-template-id naming the template C with its
 * own template parameters as arguments."  Each is kept in function_templates
 * as a deduction guide's record (implicit_guide: its declarator the
 * constructor's parameters-and-qualifiers), once per class template.  A
 * constructor whose parameters are not analysed in the guide's scope (the
 * injected-class-name of the copy constructor) makes no guide.  The class
 * template's pack and template template-parameters make none.
 */
void semantic_analysis::implicit_deduction_guides(uint32_t ct)
{
    function_template &rec = function_templates[ct - 1];
    if (rec.guides_made)
        return;
    rec.guides_made = true;
    const function_template c = rec;
    if (!c.definition || !c.definition.is(syntax_kind::class_specifier))
        return;
    for (uint32_t i = 0; i < c.parameter_count; i++)
        if (template_parameter_is_pack(c.first_parameter + i) ||
            entity_table[template_parameter_entities[c.first_parameter + i]]
                    .kind == entity_kind::template_entity)
            return;
    auto members = c.definition.class_specifier().member_specification();
    for (SyntaxElement mem : members.elements()) {
        size_t unsupported_before = unsupported.size();
        entity_index saved_scope = current_scope;
        entity_index saved_class = defining_class;
        uint32_t saved_cv = function_cv;
        owned_array<parameter_meaning> saved_params =
            static_cast<owned_array<parameter_meaning> &&>(function_parameters);
        analysis_result r = implicit_guide_member(ct, mem);
        current_scope = saved_scope;
        defining_class = saved_class;
        function_cv = saved_cv;
        function_parameters =
            static_cast<owned_array<parameter_meaning> &&>(saved_params);
        if (r != analysis_result::well_formed)
            unsupported.resize(unsupported_before);
    }
}

analysis_result semantic_analysis::implicit_guide_member(uint32_t ct,
                                                         SyntaxElement mem)
{
    const function_template c = function_templates[ct - 1];
    uint32_t class_name = entity_table[c.entity].name;
    owned_array<SyntaxRef> parameters;
    for (uint32_t i = 0; i < c.parameter_count; i++)
        parameters.append(template_parameter_nodes[c.first_parameter + i]);
    SyntaxElement decl = mem;
    if (mem.is(syntax_kind::template_declaration)) {
        if (mem.size() != 2 || !mem[0].is(syntax_kind::template_head))
            return analysis_result::unsupported;
        owned_array<SyntaxRef> own;
        if (!template_head_list(mem[0], own))
            return analysis_result::unsupported;
        for (SyntaxRef p : own)
            parameters.append(p);
        decl = mem[1];
    }
    if (!decl.is(syntax_kind::function_definition,
                 syntax_kind::member_declaration))
        return analysis_result::unsupported;
    uint32_t at = leading_gnu_attributes(decl.index());
    bool is_explicit = false;
    if (at < decl.size() && decl[at].is(syntax_kind::decl_specifier_seq)) {
        for (token_index t = decl[at].first_token(); t < decl[at].end_token();
             t++)
            if (SyntaxElement::from_token(t).is(standard_keyword::explicit_))
                is_explicit = true;
        at++;
    }
    if (at >= decl.size())
        return analysis_result::unsupported;
    DeclaratorParts ctor(decl[at]);
    if (!ctor.direct || !ctor.id.is_identifier() ||
        tokens.identifier(ctor.id.token()) != class_name || !ctor.parameters())
        return analysis_result::unsupported;
    entity_index outer = entity_table[c.entity].enclosing;
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY(declared, template_parameter_scope(c.declaration, parameters, outer, 0,
                                            index, scope, first));
    /* the return type: the class template with its own parameters */
    owned_array<template_argument> own_arguments;
    for (uint32_t i = 0; i < c.parameter_count; i++) {
        entity_index pe = template_parameter_entities[first + i];
        template_argument a;
        if (entity_table[pe].kind == entity_kind::typedef_name) {
            a.kind = template_argument::kind_t::type;
            a.type = entity_table[pe].type;
        } else {
            a.kind = template_argument::kind_t::value;
            a.type = entity_table[pe].type;
            a.template_entity = pe;
            a.value = i;
        }
        own_arguments.append(a);
    }
    qualified_type type = qualified_type::make(
        dependent_specialization(c.entity, own_arguments), cv_none);
    current_scope = scope;
    defining_class = 0;
    function_cv = 0;
    function_parameters.clear();
    VTRY(typed, function(ctor.parameters().index(), type));
    if (index != function_templates.size()) {
        index = static_cast<uint32_t>(function_templates.size());
        for (size_t i = 0; i < parameters.size(); i++) {
            entity_index e = template_parameter_entities[first + i];
            if (entity_table[e].kind != entity_kind::template_entity)
                entity_table[e].instantiation = index + 1;
        }
    }
    function_template record =
        new_template(class_name, outer, c.declaration, index, first,
                     static_cast<uint32_t>(parameters.size()));
    record.declarator = ctor.parameters().index();
    record.guide_of = ct;
    record.implicit_guide = true;
    record.explicit_guide = is_explicit;
    record.guide_of_template = mem.is(syntax_kind::template_declaration);
    entity_table[record.entity].type = type;
    if (!is_namespace(outer))
        entity_table[record.entity].inline_ = true;
    function_templates.append(record);
    return analysis_result::well_formed;
}

analysis_result
semantic_analysis::implicit_guide_type(const function_template &ft,
                                       const template_argument *arguments,
                                       SyntaxRef at, qualified_type &type)
{
    uint32_t ct = ft.guide_of;
    entity_index c = 0;
    VTRY(r, instantiate_class(at, ct, arguments,
                              function_templates[ct - 1].parameter_count, c));
    type = entity_table[c].type;
    function_cv = 0;
    function_parameters.clear();
    return function(ft.declarator, type);
}

} // namespace cplusplus
