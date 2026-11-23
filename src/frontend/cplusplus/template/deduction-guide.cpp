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
        return not_supported(t, "temp.deduct.guide");
    owned_array<SyntaxRef> parameters;
    VTRY(head, template_head_parameters(t, parameters, true, true, true, true));
    if (trailing_constraint(t) || head_constraint(t))
        return not_supported(t, "temp.constr");
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

} // namespace cplusplus
