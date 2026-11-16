/*
 * expr.prim.id.cpp: [expr.prim.id] Names, as far as declarators use them:
 * the declarator-id of [dcl.decl] and the nested-name-specifier of
 * [expr.prim.id.qual].  The id-expressions of expressions are P3's.
 *
 *     id-expression:
 *         unqualified-id
 *         qualified-id
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * [expr.prim.id.qual]:
 *
 *     nested-name-specifier:
 *         ::
 *         type-name ::
 *         namespace-name ::
 *         decltype-specifier ::
 *         nested-name-specifier identifier ::
 *         nested-name-specifier template_opt simple-template-id ::
 *
 * Whether a name before '::' is a type-name or a namespace-name is the
 * lookup's ([basic.lookup.qual]/1: "lookup of the name preceding that ::
 * considers only namespaces, types, and templates whose specializations are
 * types").  *scope is the namespace the specifier names when it names one;
 * *known is false once it names a class, whose members the parser does not
 * know until P5.  decltype-specifier :: is P3's, simple-template-id :: P7's.
 */
bool parser::nested_name_specifier_follows(unsigned ahead)
{
    return at(preprocessing_op_or_punc::COLON_COLON, ahead) ||
           (at_identifier(ahead) &&
            at(preprocessing_op_or_punc::COLON_COLON, ahead + 1));
}

node_index parser::nested_name_specifier(scope_index *scope, bool *known)
{
    size_t mark = begin_node();
    token_index first = position;
    bool qualified = false;
    *known = true;
    *scope = names.current();
    if (at(preprocessing_op_or_punc::COLON_COLON)) {
        add_token();
        *scope = declared_names::global_namespace;
        qualified = true;
    }
    while (at_identifier() && at(preprocessing_op_or_punc::COLON_COLON, 1)) {
        uint32_t name = tokens.identifier(position);
        if (*known) {
            scope_index found = 0;
            name_kind kind;
            if (qualified)
                kind = names.lookup_qualified(*scope, name, &found);
            else
                kind = names.lookup_unqualified(name, &found);
            if (kind == name_kind::namespace_name)
                *scope = found;
            else if (kind == name_kind::type_name)
                *known = false; /* a class or enumeration: P5 */
            else
                /* [basic.lookup.qual]/1: "If the name found does not
                   designate a namespace or a class, enumeration, or
                   dependent type, the program is ill-formed": a semantic
                   rule; a name declared where the parser cannot see it
                   (in a class, a template) is the same to it */
                return beyond_this_step();
        }
        add_token();
        add_token();
        qualified = true;
    }
    if (at(standard_keyword::template_) || at(standard_keyword::decltype_) ||
        (at_identifier() && at(preprocessing_op_or_punc::LESS, 1)))
        return beyond_this_step();
    if (position == first)
        return 0;
    return finish_node(syntax_kind::nested_name_specifier, mark, first);
}

/*
 * [dcl.decl]/4:
 *
 *     declarator-id:
 *         ..._opt id-expression
 *
 * [expr.prim.id.unqual]:
 *
 *     unqualified-id:
 *         identifier
 *         operator-function-id
 *         conversion-function-id
 *         literal-operator-id
 *         ~ type-name
 *         ~ decltype-specifier
 *         template-id
 *
 * [expr.prim.id.qual]:
 *
 *     qualified-id:
 *         nested-name-specifier template_opt unqualified-id
 *
 * The operator names are P5's, template-ids and the ... of a pack P7's.
 * An identifier passes through; ~ type-name is an unqualified-id node.
 */
node_index parser::declarator_id(declarator_facts &facts)
{
    if (at(preprocessing_op_or_punc::ELLIPSIS))
        return beyond_this_step();
    size_t mark = begin_node();
    token_index first = position;
    scope_index scope = 0;
    bool known = false;
    node_index qualifier = nested_name_specifier(&scope, &known);
    if (failed())
        return 0;
    facts.has_id = true;
    facts.id_first = position;
    if (at(standard_keyword::operator_) || at(standard_keyword::template_))
        return beyond_this_step();
    node_index unqualified = 0;
    if (at(preprocessing_op_or_punc::TILDE)) {
        /* ~ type-name: a destructor ([class.dtor]) */
        size_t tilde_mark = begin_node();
        add_token();
        if (at(standard_keyword::decltype_))
            return beyond_this_step();
        if (!at_identifier())
            return expected("a type-name after '~'", "expr.prim.id.unqual");
        add_token();
        unqualified = finish_node(syntax_kind::unqualified_id, tilde_mark,
                                  facts.id_first);
    } else if (at_identifier()) {
        if (!qualifier)
            facts.name = tokens.identifier(position);
        add_token();
    } else {
        return expected("an identifier", "expr.prim.id.unqual");
    }
    (void)unqualified;
    if (at(preprocessing_op_or_punc::LESS))
        return beyond_this_step(); /* a template-id: P7 */
    facts.id_last = position - 1;
    if (!qualifier)
        return 0; /* an unqualified-id passes through */
    facts.qualified_id = true;
    return finish_node(syntax_kind::qualified_id, mark, first);
}

} // namespace cplusplus
