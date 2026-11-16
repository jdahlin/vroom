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
        arguments = is_template_kind(names.lookup_qualified(
            scope, tokens.identifier(name_first)));
    }
    if (arguments && at(preprocessing_op_or_punc::LESS)) {
        template_id_rest(name_mark, name_first, syntax_kind::simple_template_id);
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

} // namespace cplusplus
