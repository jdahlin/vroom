/*
 * temp.res.cpp: [temp.res] Name resolution.
 *
 *     typename-specifier:
 *         typename nested-name-specifier identifier
 *         typename nested-name-specifier template_opt simple-template-id
 *
 * [temp.res]/2: "A name used in a template declaration or definition and
 * that is dependent on a template-parameter is assumed not to name a type
 * unless the applicable name lookup finds a type name or the name is
 * qualified by the keyword typename."  [temp.names]/4: in a
 * typename-specifier "a < token is always assumed to introduce a
 * template-argument-list".  Which names are assumed to name a type without
 * typename ([temp.res]/4-5) is decided where they are
 * (decl_specifier_facts::implicit_typename, parser::type_id_only).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::typename_specifier()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* typename */
    scope_index scope = 0;
    bool known = false;
    node_index qualifier = nested_name_specifier(&scope, &known);
    if (failed())
        return 0;
    if (!qualifier) {
        /* typename before another decl-specifier: Microsoft's extension,
           P9's */
        const implementation_defined_behavior &b = tokens.files.behavior;
        if (b.microsoft_extensions || b.microsoft_compatibility)
            return beyond_this_step();
        return expected("a nested-name-specifier after typename", "temp.res");
    }
    bool template_keyword = accept(standard_keyword::template_);
    if (!at_identifier())
        return expected("an identifier after the nested-name-specifier",
                        "temp.res");
    size_t name_mark = begin_node();
    token_index name_first = position;
    add_token();
    /* a < after a name the parser can look up begins a
       template-argument-list when the name is a template's; after a
       dependent nested-name-specifier clang requires template ([temp.names]
       /4 of C++20; P1787R6 later assumes it) */
    bool arguments = template_keyword;
    if (!arguments && at(preprocessing_op_or_punc::LESS)) {
        if (!known)
            return violates("template before a dependent template-name",
                            "temp.names", name_first);
        arguments = is_template_kind(
            names.lookup_qualified(scope, tokens.identifier(name_first)));
    }
    if (arguments && at(preprocessing_op_or_punc::LESS)) {
        template_id_rest(name_mark, name_first,
                         syntax_kind::simple_template_id);
        if (failed())
            return 0;
    } else if (template_keyword) {
        return expected("a simple-template-id after template", "temp.res");
    }
    /* the type is one the parser cannot look into: a dependent one, as far
       as what follows is concerned */
    names.dependent_found++;
    return finish_node(syntax_kind::typename_specifier, mark, first);
}

/*
 * [temp.res]/3: "A typename-specifier denotes the type or class template
 * denoted by the simple-type-specifier ([dcl.type.simple]) formed by
 * omitting the keyword typename.  The usual qualified name lookup
 * ([basic.lookup.qual]) is used to find the qualified-id even in the
 * presence of typename."  With a dependent nested-name-specifier the name
 * is a member of an unknown specialization ([temp.dep.type]/6), a
 * dependent type (/9), known per specialization, where the declaration is
 * analysed again with the template-parameters' names bound to the
 * arguments.  Otherwise the name found shall be a type (clang rejects one
 * that is not, at the instantiation; analysis says not supported).
 */
analysis_result semantic_analysis::typename_type(SyntaxRef n, type_index &type,
                                                 uint32_t &cv)
{
    auto node = n;
    type = 0;
    cv = 0;
    /* a type-requirement ([expr.prim.req.type]) is one with a ';' */
    uint32_t count = node.size();
    if (node.is(syntax_kind::type_requirement))
        count--;
    if (count < 3 || count > 4 ||
        !node[1].is(syntax_kind::nested_name_specifier))
        return not_supported(n, "temp.res");
    SyntaxElement last = node[count - 1];
    if (last.is(syntax_kind::simple_template_id)) {
        /* typename nested-name-specifier template_opt simple-template-id:
           [temp.names]/1's template of the class or namespace named (a
           member alias template, a member class template).  Of a
           dependent nested-name-specifier ([temp.res]/5, [temp.names]/5:
           typename T::template X<U>), a dependent type known by how it
           is written; an instantiation looks the template up in the
           substituted class */
        nominated_scope scope;
        analysis_result r = nested_name_specifier(node[1], scope, true);
        if (r != analysis_result::well_formed)
            return r;
        if (scope.dependent) {
            type = dependent_written_type(n);
            return analysis_result::well_formed;
        }
        if (!scope.entity)
            return not_supported(n, "temp.res");
        return template_type(syntax(last.index()), scope.entity, true, type,
                             &cv);
    }
    if (count != 3)
        return not_supported(n, "temp.res");
    if (!node[2].is_identifier())
        return not_supported(n, "temp.res");
    token_index t = node[2].token();
    uint32_t name = tokens.identifier(t);
    nominated_scope scope;
    analysis_result r = nested_name_specifier(node[1], scope, true);
    if (r != analysis_result::well_formed)
        return r;
    if (scope.dependent) {
        type = dependent_member_type(scope.dependent, name);
        return analysis_result::well_formed;
    }
    lookup_result found =
        lookup_qualified(scope.entity, name, lookup_filter::any);
    if (found.ambiguous)
        return ill_formed_at_token(t,
                                   "the use of a name is ill-formed if it "
                                   "does not allow a unique declaration to "
                                   "be chosen from those found in the "
                                   "namespaces",
                                   "namespace.qual");
    entity_index e = found.entity;
    if (!e && !found.unknown) {
        analysis_result m = missing_member(t, scope.entity);
        if (m != analysis_result::well_formed)
            return m;
    }
    return named_member_type(n, t, e, "temp.res", type, cv);
}

/* [temp.deduct]/11: "Attempting to use a type in a nested-name-specifier
   of a qualified-id when that type does not contain the specified member"
   (a deduction failure): name t not found in class scope, substituting */
analysis_result semantic_analysis::missing_member(token_index t,
                                                  entity_index scope)
{
    if (!substituting || !scope ||
        entity_table[scope].kind != entity_kind::class_type ||
        !entity_table[scope].defined || entity_table[scope].unknown_names)
        return analysis_result::well_formed;
    return ill_formed_at_token(t,
                               "the name shall represent one or more "
                               "members of that class or of one of its "
                               "base classes",
                               "class.qual");
}

/* [temp.dep.type]/9: typename Q::name for a dependent Q; [temp.over.link]
   compares declarations by their types, so the same Q and name give the
   same type */
type_index semantic_analysis::dependent_member_type(type_index qualifier,
                                                    uint32_t name)
{
    return dependent_type(dependent_members, entity_kind::dependent_member,
                          name, qualifier);
}

/* a dependent type of kind, hash-consed in list on name and value: the
   same for the same parts ([temp.over.link]) */
type_index semantic_analysis::dependent_type(owned_array<entity_index> &list,
                                             entity_kind kind, uint32_t name,
                                             uint64_t value)
{
    for (entity_index e : list)
        if (entity_table[e].value == value && entity_table[e].name == name)
            return entity_table[e].type.unqualified();
    entity_index e = entity_table.make(kind, name, 0);
    entity_table[e].value = value;
    entity_table[e].type = qualified_type::make(
        type_table.of_entity(type_kind::dependent, e), cv_none);
    list.append(e);
    return entity_table[e].type.unqualified();
}

} // namespace cplusplus
