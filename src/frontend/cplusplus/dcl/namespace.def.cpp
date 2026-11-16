/*
 * namespace.def.cpp: [namespace.def] Namespace definition.
 *
 *     namespace-definition:
 *             named-namespace-definition
 *             unnamed-namespace-definition
 *             nested-namespace-definition
 *
 *     named-namespace-definition:
 *             inline_opt namespace attribute-specifier-seq_opt identifier { namespace-body }
 *
 *     unnamed-namespace-definition:
 *             inline_opt namespace attribute-specifier-seq_opt { namespace-body }
 *
 *     nested-namespace-definition:
 *             namespace enclosing-namespace-specifier :: inline_opt identifier { namespace-body }
 *
 *     enclosing-namespace-specifier:
 *             identifier
 *             enclosing-namespace-specifier :: inline_opt identifier
 *
 *     namespace-body:
 *             declaration-seq_opt
 *
 * namespace-body passes its declaration-seq through.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::namespace_definition()
{
    size_t mark = begin_node();
    token_index first = position;
    bool inline_ = accept(standard_keyword::inline_);
    add_token(); /* namespace */
    token_index attributes_at = position;
    node_index attributes = attribute_specifier_seq();
    if (failed())
        return 0;
    if (at(standard_keyword::extension))
        return beyond_this_step();
    syntax_kind kind = syntax_kind::unnamed_namespace_definition;
    unsigned entered = 0;
    if (at_identifier() && at(preprocessing_op_or_punc::COLON_COLON, 1)) {
        if (attributes)
            return violates("no attribute-specifier-seq in a "
                            "nested-namespace-definition",
                            "namespace.def", attributes_at);
        /* [namespace.def]/3 (C++20's nested-namespace-definition):
           namespace A::B::C { } is namespace A { namespace B { namespace
           C { } } }; "inline" may precede each but the first */
        if (inline_)
            return expected("no 'inline' before a nested-namespace-definition",
                            "namespace.def");
        kind = syntax_kind::nested_namespace_definition;
        size_t enclosing_mark = begin_node();
        token_index enclosing_first = position;
        unsigned names_in_specifier = 0;
        for (;;) {
            bool inline_part = false;
            if (names_in_specifier && at(standard_keyword::inline_)) {
                add_token();
                inline_part = true;
            }
            if (!at_identifier())
                return expected("an identifier", "namespace.def");
            names.enter_namespace(tokens.identifier(position), inline_part);
            entered++;
            add_token();
            names_in_specifier++;
            if (!at(preprocessing_op_or_punc::COLON_COLON))
                break;
            if (!at_identifier(1) && !at(standard_keyword::inline_, 1))
                return expected("an identifier after '::'", "namespace.def");
            /* the last identifier is the nested-namespace-definition's:
               the specifier is what comes before its '::' */
            bool last = at_identifier(1) &&
                        !at(preprocessing_op_or_punc::COLON_COLON, 2);
            if (at(standard_keyword::inline_, 1))
                last = at_identifier(2) &&
                       !at(preprocessing_op_or_punc::COLON_COLON, 3);
            if (last) {
                if (names_in_specifier > 1)
                    finish_node(syntax_kind::enclosing_namespace_specifier,
                                enclosing_mark, enclosing_first);
                add_token(); /* :: */
                bool inline_last = accept(standard_keyword::inline_);
                names.enter_namespace(tokens.identifier(position), inline_last);
                entered++;
                add_token();
                break;
            }
            add_token(); /* :: */
        }
    } else if (at_identifier()) {
        kind = syntax_kind::named_namespace_definition;
        names.enter_namespace(tokens.identifier(position), inline_);
        entered++;
        add_token();
    } else {
        names.enter_namespace(0, inline_);
        entered++;
    }
    if (!accept(preprocessing_op_or_punc::LEFT_BRACE))
        return expected("'{' to begin the namespace-body", "namespace.def");
    enclosing_braces++;
    declaration_seq(true);
    enclosing_braces--;
    for (unsigned i = 0; i < entered; i++)
        names.leave();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_BRACE))
        return expected("'}' to end the namespace-body", "namespace.def");
    return finish_node(kind, mark, first);
}

/*
 * [namespace.alias]:
 *
 *     namespace-alias-definition:
 *             namespace identifier = qualified-namespace-specifier ;
 *
 *     qualified-namespace-specifier:
 *         nested-name-specifier_opt namespace-name
 *
 * [namespace.alias]/2: "The identifier in a namespace-alias-definition
 * becomes a namespace-alias and denotes the namespace denoted by the
 * qualified-namespace-specifier."
 */
node_index parser::namespace_alias_definition()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* namespace */
    uint32_t alias = tokens.identifier(position);
    add_token();
    add_token(); /* = */
    size_t specifier_mark = begin_node();
    token_index specifier_first = position;
    scope_index scope = 0;
    bool known = false;
    node_index qualifier = nested_name_specifier(&scope, &known);
    if (failed())
        return 0;
    if (!at_identifier())
        return expected("a namespace-name", "namespace.alias");
    scope_index nominated = 0;
    name_kind kind;
    if (qualifier)
        kind = names.lookup_qualified(scope, tokens.identifier(position),
                                      &nominated);
    else
        kind =
            names.lookup_unqualified(tokens.identifier(position), &nominated);
    if (kind != name_kind::namespace_name)
        /* [namespace.alias]/1: a namespace-name is required; a name the
           parser does not know may be a later step's */
        return beyond_this_step();
    add_token();
    if (qualifier)
        finish_node(syntax_kind::qualified_namespace_specifier, specifier_mark,
                    specifier_first);
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the namespace-alias-definition",
                        "namespace.alias");
    names.declare_namespace_alias(alias, nominated);
    return finish_node(syntax_kind::namespace_alias_definition, mark, first);
}

/*
 * [namespace.udir]:
 *
 *     using-directive:
 *         attribute-specifier-seq_opt using namespace nested-name-specifier_opt namespace-name ;
 *
 * [namespace.udir]/2: "A using-directive specifies that the names in the
 * nominated namespace can be used in the scope in which the using-directive
 * appears after the using-directive."
 */
node_index parser::using_directive()
{
    size_t mark = begin_node();
    token_index first = position;
    attribute_specifier_seq();
    if (failed())
        return 0;
    add_token(); /* using */
    add_token(); /* namespace */
    scope_index scope = 0;
    bool known = false;
    node_index qualifier = nested_name_specifier(&scope, &known);
    if (failed())
        return 0;
    if (!at_identifier())
        return expected("a namespace-name", "namespace.udir");
    scope_index nominated = 0;
    name_kind kind;
    if (qualifier)
        kind = names.lookup_qualified(scope, tokens.identifier(position),
                                      &nominated);
    else
        kind =
            names.lookup_unqualified(tokens.identifier(position), &nominated);
    if (kind != name_kind::namespace_name)
        return beyond_this_step();
    add_token();
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the using-directive", "namespace.udir");
    names.use_namespace(nominated);
    return finish_node(syntax_kind::using_directive, mark, first);
}

/*
 * [dcl.link]:
 *
 *     linkage-specification:
 *         extern string-literal { declaration-seq_opt }
 *         extern string-literal declaration
 *
 * [dcl.link]/2: "The string-literal indicates the required language
 * linkage"; which ones are supported is implementation-defined, a semantic
 * matter.
 */
node_index parser::linkage_specification()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* extern */
    add_token(); /* string-literal */
    if (accept(preprocessing_op_or_punc::LEFT_BRACE)) {
        enclosing_braces++;
        declaration_seq(true);
        enclosing_braces--;
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_BRACE))
            return expected("'}' to end the linkage-specification", "dcl.link");
    } else {
        declaration();
        if (failed())
            return 0;
    }
    return finish_node(syntax_kind::linkage_specification, mark, first);
}

} // namespace cplusplus
