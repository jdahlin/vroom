/*
 * class-template.cpp: [temp.class] Class templates: a class template's
 * definition and declaration.
 * Split from declared-templates.cpp, which has [temp.pre]'s declarations of
 * templates and what the kinds share.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"
#include "declared-templates.h"

namespace cplusplus
{

/*
 * [temp.class]/1: "A class template defines the layout and operations for
 * an unbounded set of related types."  One defined at namespace scope
 * (template<class T> struct S { ... };): its class-specifier is analysed
 * per specialization (instantiate_class), the template's name bound to a
 * held entity that a simple-template-id finds (template_type).
 */
namespace
{
/* [class.friend]/2: while a class template's template-head and parameters
   are analysed, a friend class template of its name is the befriended
   entity (accessible) */
struct friend_head_scope_guard {
    entity_index &scope;
    uint32_t &name;
    friend_head_scope_guard(entity_index &s, uint32_t &n, entity_index in,
                            uint32_t called)
        : scope(s), name(n)
    {
        scope = in;
        name = called;
    }
    ~friend_head_scope_guard()
    {
        scope = 0;
        name = 0;
    }
};
} // namespace

analysis_result
semantic_analysis::class_template_definition(SyntaxRef n, SyntaxRef specifier)
{
    entity_index outer = current_scope;
    owned_array<SyntaxRef> parameters;
    /* class-head: class-key identifier base-clause_opt */
    if (!specifier.size() || !specifier[0].is(syntax_kind::class_head))
        return not_supported(n, "temp.class");
    auto head = specifier[0];
    SyntaxElement head_name = class_head_name(head);
    bool specialization =
        head_name && head_name.is(syntax_kind::simple_template_id);
    /* class-head-name: nested-name-specifier_opt class-name, the class-name
       a simple-template-id: a partial specialization of a member of a
       namespace or class ([temp.class.spec]/1) */
    if (head_name && head_name.qualified_class_head_name() &&
        head_name.size() == 2 &&
        head_name[1].is(syntax_kind::simple_template_id))
        specialization = true;
    uint32_t called = 0;
    if (!specialization && head_name && head_name.is_token() &&
        tokens[head_name.token()].kind == token_kind::identifier)
        called = tokens.identifier(head_name.token());
    friend_head_scope_guard guard(friend_head_scope, friend_head_name, outer,
                                  called);
    VTRY(r, template_head_parameters(n, parameters, true, true, specialization,
                                     true));
    /* a partial specialization's requires-clause is checked where it is
       matched (matching_specialization); the primary's where a
       specialization is named (template_type) */
    if (specialization) {
        return class_specialization_definition(n, specifier, parameters);
    }
    if (auto qualified = head_name.qualified_class_head_name()) {
        if (!qualified.nested_name_specifier() || !qualified.identifier())
            return not_supported(n, "temp.class");
        nominated_scope ns;
        VTRY_SET(r, nested_name_specifier(
                        qualified.nested_name_specifier().index(), ns));
        if (!is_class_entity(ns.entity) && !is_namespace(ns.entity))
            return not_supported(n, "temp.class");
        outer = ns.entity;
        head_name = qualified.identifier();
    }
    if (!head_name.is_token() ||
        tokens[head_name.token()].kind != token_kind::identifier)
        return not_supported(n, "temp.class");
    uint32_t name = tokens.identifier(head_name.token());
    /* [temp.mem]/3: "A member function template shall not be virtual.":
       diagnosed in the definition, which is analysed only per
       specialization (member_function_template's check) */
    if (specifier.is(syntax_kind::class_specifier))
        for (auto member :
             specifier.class_specifier().member_specification().elements()) {
            if (!member.is(syntax_kind::template_declaration) ||
                member.size() != 2 ||
                !member[1].is(syntax_kind::member_declaration,
                              syntax_kind::function_definition))
                continue;
            SyntaxRef inner = member[1];
            uint32_t lead = leading_gnu_attributes(inner);
            if (lead >= inner.size() ||
                !inner[lead].is(syntax_kind::decl_specifier_seq))
                continue;
            for (auto x : inner[lead].elements())
                if (x.is(standard_keyword::virtual_))
                    return ill_formed_at_token(
                        x.token(),
                        "a member function template shall not be virtual",
                        "temp.mem");
        }
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY_SET(r, template_parameter_scope(n, parameters, outer, name, index,
                                         scope, first));
    /* the definition of a class template declared before
       (class_template_declaration): its record, defined now */
    entity_index prior = entity_table.find(outer, name);
    uint32_t declared = class_template_of(prior);
    if (prior && declared && function_templates[declared - 1].name == prior &&
        !function_templates[declared - 1].definition &&
        !function_templates[declared - 1].primary) {
        const function_template d = function_templates[declared - 1];
        /* [temp.over.link]/6: the same template-head, its requires-clause
           equivalent (/7) */
        if (entity_table[d.entity].enclosing != outer ||
            d.parameter_count != parameters.size() ||
            !same_template_parameters(first, d.first_parameter,
                                      d.parameter_count) ||
            !same_constraints(d.declaration, n))
            return not_supported(n, "temp.class");
        VTRY(merged, merge_template_defaults(n, d.first_parameter, first,
                                             d.parameter_count, true));
        for (uint32_t i = 0; i < d.parameter_count; i++)
            if (entity_table[template_parameter_entities[first + i]].kind !=
                entity_kind::template_entity)
                entity_table[template_parameter_entities[first + i]]
                    .instantiation = declared;
        function_template &record = function_templates[declared - 1];
        record.declaration = n;
        record.definition = specifier;
        record.first_parameter = first;
        entity_table[d.entity].defined = true;
        entity_table.add_declaration(d.entity, n);
        return analysis_result::well_formed;
    }
    /* [temp.param]/14: "If a template-parameter of a class template ...
       has a default template-argument, each subsequent template-parameter
       shall either have a default template-argument supplied or be a
       template parameter pack" */
    for (size_t i = 1; i < parameters.size(); i++)
        if (template_parameter_has_default(parameters[i - 1]) &&
            !template_parameter_has_default(parameters[i]) &&
            !template_parameter_is_pack(first + static_cast<uint32_t>(i)))
            return ill_formed(parameters[i],
                              "each template-parameter after one with a "
                              "default template-argument shall have one",
                              "temp.param");
    function_template record = new_template(
        name, outer, n, index, first, static_cast<uint32_t>(parameters.size()));
    entity_index t = record.entity;
    entity_table[t].defined = true;
    record.definition = specifier;
    record.is_class = true;
    /* the name, held: one bound already declares something else too (a
       class, another template's forward declaration) */
    entity_index held = entity_table.find(outer, name);
    if (held) {
        record.unusable = true;
        entity_table[held].kind = entity_kind::none;
        note_held(held);
        entity_table[held].held_by_template = true;
        for (function_template &other : function_templates)
            if (other.name == held)
                other.unusable = true;
    } else {
        held = make_held_name(name, outer);
        entity_table.bind(outer, name, held);
    }
    record.name = held;
    function_templates.append(record);
    return analysis_result::well_formed;
}

/*
 * [temp.class]/1 with [dcl.type.elab]/2: template<class T> struct S; (a
 * class-key and an identifier).  The first declares the template, whose
 * definition comes later (class_template_definition completes its
 * record); a specialization named before then is an incomplete class
 * (instantiate_class).  Another declaration of the same template-head
 * declares it again ([temp.over.link]/6's equivalent heads; default
 * template-arguments merged across declarations are only the
 * definition's, [temp.param]/10).
 */
analysis_result
semantic_analysis::class_template_declaration(SyntaxRef n, SyntaxRef elaborated)
{
    entity_index outer = current_scope;
    if (elaborated.size() == 2 && elaborated[0].is_token() &&
        elaborated[1].is(syntax_kind::simple_template_id)) {
        /* [temp.class.spec]/1: template<class A> class S<bool, A>; a
           partial specialization declared, its definition later */
        standard_keyword k = elaborated[0].keyword();
        if (k != standard_keyword::class_ && k != standard_keyword::struct_)
            return not_supported(n, "temp.class.spec");
        owned_array<SyntaxRef> list;
        VTRY(r, template_head_parameters(n, list, true, true, true, true));
        return class_specialization_definition(n, elaborated, list);
    }
    /* the attribute-specifier-seq after the class-key, left out when none
       changes what vcc generates ([dcl.type.elab]/1), one without an
       argument (clang checks preferred_name's against the template) */
    uint32_t at = 1;
    if (elaborated.size() == 3 &&
        class_head_attributes_without_effect(elaborated[1]) &&
        elaborated[1].size() == 5 &&
        !elaborated[1][2].is(syntax_kind::attribute))
        at = 2;
    if (elaborated.size() != at + 1 || !elaborated[0].is_token() ||
        !elaborated[at].is_identifier())
        return not_supported(n, "temp.class");
    standard_keyword key = elaborated[0].keyword();
    if (key != standard_keyword::class_ && key != standard_keyword::struct_ &&
        key != standard_keyword::union_)
        return not_supported(n, "temp.class");
    owned_array<SyntaxRef> parameters;
    /* [temp.pre]/9: a requires-clause in its template-head */
    uint32_t name = tokens.identifier(elaborated[at].token());
    friend_head_scope_guard guard(friend_head_scope, friend_head_name, outer,
                                  name);
    VTRY(r, template_head_parameters(n, parameters, true, true, false, true));
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY_SET(r, template_parameter_scope(n, parameters, outer, name, index,
                                         scope, first));
    entity_index held = entity_table.find(outer, name);
    if (held) {
        /* a redeclaration of the template, with no default
           template-argument (/12 forbids one given twice; merging is
           not analysed) */
        uint32_t ct = class_template_of(held);
        if (!ct || function_templates[ct - 1].name != held ||
            function_templates[ct - 1].primary)
            return not_supported(n, "temp.class");
        const function_template d = function_templates[ct - 1];
        if (entity_table[d.entity].enclosing != outer ||
            d.parameter_count != parameters.size() ||
            !same_template_parameters(first, d.first_parameter,
                                      d.parameter_count) ||
            !same_constraints(d.declaration, n))
            return not_supported(n, "temp.class");
        for (SyntaxRef x : parameters)
            if (template_parameter_has_default(x))
                return not_supported(n, "temp.param");
        entity_table.add_declaration(d.entity, n);
        return analysis_result::well_formed;
    }
    /* [temp.param]/14 (a later declaration may not supply the defaults
       before) */
    if (default_missing_after_one(parameters))
        return not_supported(n, "temp.param");
    function_template record = new_template(
        name, outer, n, index, first, static_cast<uint32_t>(parameters.size()));
    record.is_class = true;
    hold_template_name(record, name, outer);
    return analysis_result::well_formed;
}

} // namespace cplusplus
