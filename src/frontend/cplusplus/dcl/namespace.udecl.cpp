/*
 * namespace.udecl.cpp: [namespace.udecl] The using declaration.
 *
 *     using-declaration:
 *         using using-declarator-list ;
 *
 *     using-declarator-list:
 *         using-declarator ..._opt
 *         using-declarator-list , using-declarator ..._opt
 *
 * [namespace.udecl]/1: "the unqualified-id is declared in the declarative
 * region in which the using-declaration appears as a synonym for each
 * declaration introduced by the using-declarator": the name gets the kind
 * the qualified lookup finds.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::using_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* using */
    size_t list_mark = begin_node();
    token_index list_first = position;
    unsigned count = 0;
    bool ellipsis = false;
    for (;;) {
        using_declarator();
        if (failed())
            return 0;
        count++;
        if (accept(preprocessing_op_or_punc::ELLIPSIS))
            ellipsis = true;
        if (!accept(preprocessing_op_or_punc::COMMA))
            break;
    }
    if (count > 1 || ellipsis)
        finish_node(syntax_kind::using_declarator_list, list_mark, list_first);
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the using-declaration", "namespace.udecl");
    return finish_node(syntax_kind::using_declaration, mark, first);
}

/*
 *     using-declarator:
 *         typename_opt nested-name-specifier unqualified-id
 *
 * The unqualified-ids that are operator names are P5's, template-ids P7's.
 * A using-declarator with typename names a type ([temp.res]/5); a name
 * the parser cannot look up (a class's member) is declared as nothing.
 */
node_index parser::using_declarator()
{
    size_t mark = begin_node();
    token_index first = position;
    bool typename_ = accept(standard_keyword::typename_);
    scope_index scope = 0;
    bool known = false;
    node_index qualifier = nested_name_specifier(&scope, &known);
    if (failed())
        return 0;
    if (!qualifier)
        return expected("a nested-name-specifier", "namespace.udecl");
    if (at(standard_keyword::operator_) || at(standard_keyword::template_))
        return beyond_this_step();
    uint32_t name = 0;
    if (at(preprocessing_op_or_punc::TILDE)) {
        size_t tilde_mark = begin_node();
        token_index tilde_first = position;
        add_token();
        if (at(standard_keyword::decltype_))
            return beyond_this_step();
        if (!at_identifier())
            return expected("a type-name after '~'", "expr.prim.id.unqual");
        add_token();
        finish_node(syntax_kind::unqualified_id, tilde_mark, tilde_first);
    } else if (at_identifier()) {
        name = tokens.identifier(position);
        add_token();
    } else {
        return expected("an unqualified-id", "namespace.udecl");
    }
    if (at(preprocessing_op_or_punc::LESS))
        return beyond_this_step(); /* a template-id: P7 */
    if (name) {
        name_kind kind = name_kind::not_declared;
        if (typename_)
            kind = name_kind::type_name;
        else if (known)
            kind = names.lookup_qualified(scope, name);
        if (kind != name_kind::not_declared &&
            kind != name_kind::namespace_name)
            names.declare(name, kind);
    }
    return finish_node(syntax_kind::using_declarator, mark, first);
}

} // namespace cplusplus
