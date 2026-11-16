/*
 * expr.prim.id.cpp: [expr.prim.id] Names: the id-expression of an
 * expression, the declarator-id of [dcl.decl] and the
 * nested-name-specifier of [expr.prim.id.qual].
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
 * types").  *scope is the namespace or class the specifier names; *known
 * is false once it names a type whose members the parser does not know (an
 * enumeration, a typedef-name, a class it has no member-specification of,
 * a member of an unknown base); so it is after decltype-specifier ::
 * ([expr.prim.id.qual]/1: "The type denoted by a decltype-specifier in a
 * nested-name-specifier shall be a class or enumeration type").
 * simple-template-id :: is P7's.
 */
bool parser::nested_name_specifier_follows(unsigned ahead)
{
    return at(preprocessing_op_or_punc::COLON_COLON, ahead) ||
           (at_identifier(ahead) &&
            at(preprocessing_op_or_punc::COLON_COLON, ahead + 1));
}

node_index parser::nested_name_specifier(scope_index *scope, bool *known,
                                         bool template_ids)
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
    } else if (at(standard_keyword::decltype_)) {
        decltype_specifier();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::COLON_COLON))
            return expected("'::' after the decltype-specifier",
                            "expr.prim.id.qual");
        *known = false;
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
            bool in_class =
                qualified && names.kind_of(*scope) == scope_kind::class_;
            if (kind == name_kind::namespace_name)
                *scope = found;
            else if (kind == name_kind::type_name && found &&
                     names.kind_of(found) == scope_kind::class_)
                *scope = found; /* a class whose members the parser knows */
            else if (kind == name_kind::type_name)
                *known = false; /* an enumeration, a typedef-name */
            else if (kind == name_kind::not_declared && in_class)
                /* a member of a base the parser does not know */
                *known = false;
            else
                /* [basic.lookup.qual]/1: "If the name found does not
                   designate a namespace or a class, enumeration, or
                   dependent type, the program is ill-formed": a semantic
                   rule; a name declared where the parser cannot see it
                   (in a template) is the same to it */
                return beyond_this_step();
        }
        add_token();
        add_token();
        qualified = true;
    }
    /* a template-id, in the specifier or after it: P7.  An expression's
       caller decides what a '<' after its name is (template_ids false). */
    if (at(standard_keyword::template_) ||
        (template_ids && at_identifier() &&
         at(preprocessing_op_or_punc::LESS, 1)))
        return beyond_this_step();
    if (position == first)
        return 0;
    return finish_node(syntax_kind::nested_name_specifier, mark, first);
}

/*
 * The id-expression of an expression ([expr.prim.id]/1):
 *
 *     id-expression:
 *         unqualified-id
 *         qualified-id
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
 *     qualified-id:
 *         nested-name-specifier template_opt unqualified-id
 *
 * An identifier passes through; ~ type-name is an unqualified-id node, and
 * only after a nested-name-specifier or, with member, after . or ->
 * ([expr.unary.op]/10: "a ~ followed by a type-name or decltype-specifier
 * in a member access expression or qualified-id is unambiguously parsed as
 * a destructor name").  The operator names are over.oper.cpp's; template
 * and template-ids are P7's.
 *
 * [expr.prim.id.unqual]/1: "An identifier is only an id-expression if it
 * has been suitably declared", which is the semantic analysis's to check.
 * What the grammar needs is whether a '<' after the name begins a
 * template-argument-list ([temp.names]/3: "When a name is considered to be
 * a template-name, and it is followed by a <, the < is always taken as the
 * delimiter of a template-argument-list and never as the less-than
 * operator"): it does not after a name declared as a variable, function or
 * enumerator, and after any other name the tokens are P7's to read.  A
 * member's name ([basic.lookup.classref]/1: "The identifier is first looked
 * up in the class of the object expression") and a name of a class's scope
 * are unknown here, a member of the object's class until types are known
 * (the class of the object expression is the semantic analysis's).
 */
node_index parser::id_expression(bool member)
{
    size_t mark = begin_node();
    token_index first = position;
    scope_index scope = 0;
    bool known = true;
    node_index qualifier = 0;
    if (nested_name_specifier_follows(0) || at(standard_keyword::decltype_)) {
        qualifier = nested_name_specifier(&scope, &known, false);
        if (failed())
            return 0;
    }
    if (at(standard_keyword::template_))
        return beyond_this_step();
    if (at(standard_keyword::operator_)) {
        operator_unqualified_id();
        if (failed())
            return 0;
    } else if (at(preprocessing_op_or_punc::TILDE) && (qualifier || member)) {
        size_t tilde_mark = begin_node();
        token_index tilde_first = position;
        add_token();
        if (at(standard_keyword::decltype_)) {
            decltype_specifier();
            if (failed())
                return 0;
        } else if (at_identifier()) {
            add_token();
            if (at(preprocessing_op_or_punc::LESS))
                return beyond_this_step();
        } else {
            return expected("a type-name or decltype-specifier after '~'",
                            "expr.prim.id.unqual");
        }
        finish_node(syntax_kind::unqualified_id, tilde_mark, tilde_first);
    } else if (at_identifier()) {
        uint32_t name = tokens.identifier(position);
        name_kind kind = name_kind::not_declared;
        if (!member && known) {
            if (qualifier)
                kind = names.lookup_qualified(scope, name);
            else
                kind = names.lookup_unqualified(name);
        }
        add_token();
        if (kind == name_kind::template_name)
            return beyond_this_step();
        if (kind == name_kind::not_declared)
            unknown_name = true;
        /* [temp.names]/2: "A name is also considered to refer to a
           template if it is an unqualified-id followed by a < and name
           lookup either finds one or more functions or finds nothing."
           (P0846R0, C++20: before it a function's name before < is an
           operand of <) */
        if (at(preprocessing_op_or_punc::LESS) && kind == name_kind::function &&
            !qualifier &&
            tokens.files.behavior.has(
                language_feature::function_names_before_less))
            return beyond_this_step();
        if (at(preprocessing_op_or_punc::LESS) && kind != name_kind::other &&
            kind != name_kind::function)
            return beyond_this_step();
    } else if (qualifier) {
        return expected("an unqualified-id after the nested-name-specifier",
                        "expr.prim.id.qual");
    } else {
        return expected("an id-expression", "expr.prim.id");
    }
    if (!qualifier)
        return 0; /* an unqualified-id passes through */
    return finish_node(syntax_kind::qualified_id, mark, first);
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
 * template-ids and the ... of a pack are P7's.  An identifier passes
 * through; ~ type-name is an unqualified-id node, the operator names are
 * their own (over.oper.cpp).  A declarator-id that names a member of a
 * class the parser knows makes the names after it be looked up in the
 * class ([basic.scope.class]/1: "The potential scope of a declaration that
 * extends to or past the end of a class definition also extends to the
 * regions defined by its member definitions, even if the members are
 * defined lexically outside the class").
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
    if (qualifier && known && names.kind_of(scope) == scope_kind::class_) {
        facts.has_member_of = true;
        facts.member_of = scope;
    }
    if (at(standard_keyword::template_))
        return beyond_this_step();
    node_index unqualified = 0;
    if (at(standard_keyword::operator_)) {
        unqualified = operator_unqualified_id();
        if (failed())
            return 0;
    } else if (at(preprocessing_op_or_punc::TILDE)) {
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
