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
#include "frontend/cplusplus/sema.h"

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
    if (at(preprocessing_op_or_punc::COLON_COLON, ahead))
        return true;
    if (!at_identifier(ahead))
        return false;
    if (at(preprocessing_op_or_punc::COLON_COLON, ahead + 1))
        return true;
    if (!at(preprocessing_op_or_punc::LESS, ahead + 1))
        return false;
    name_kind kind =
        names.lookup_unqualified(tokens.identifier(position + ahead));
    if (!is_template_kind(kind))
        return false;
    unsigned end = angle_group_end(ahead + 1);
    return end && at(preprocessing_op_or_punc::COLON_COLON, end);
}

/*
 * One component of a nested-name-specifier at the current token, before
 * its '::': template_opt identifier or template_opt simple-template-id.
 * Its length in tokens, or 0 when it is none.  A '<' after the name begins
 * a template-argument-list when [temp.names]/2-4 say so (names_template;
 * after a dependent specifier only after template).
 */
static unsigned nested_component_length(parser &p, bool qualified, bool known,
                                        scope_index scope, bool *arguments,
                                        bool *template_keyword)
{
    unsigned i = 0;
    *template_keyword = false;
    *arguments = false;
    if (qualified && p.at(standard_keyword::template_)) {
        *template_keyword = true;
        i = 1;
    }
    if (!p.at_identifier(i))
        return 0;
    if (p.at(preprocessing_op_or_punc::COLON_COLON, i + 1))
        return i + 1;
    if (!p.at(preprocessing_op_or_punc::LESS, i + 1))
        return 0;
    bool names = *template_keyword;
    if (!names && known) {
        uint32_t name = p.tokens.identifier(p.position + i);
        name_kind kind;
        if (qualified)
            kind = p.names.lookup_qualified(scope, name);
        else
            kind = p.names.lookup_unqualified(name);
        names = p.names_template(kind, qualified, false) &&
                kind != name_kind::function && kind != name_kind::not_declared;
    }
    if (!names)
        return 0;
    unsigned end = p.angle_group_end(i + 1);
    if (!end || !p.at(preprocessing_op_or_punc::COLON_COLON, end))
        return 0;
    *arguments = true;
    return end;
}

node_index parser::nested_name_specifier(scope_index *scope, bool *known,
                                         bool template_ids)
{
    (void)template_ids;
    size_t mark = begin_node();
    token_index first = position;
    bool qualified = false;
    *known = true;
    *scope = names.current();
    qualifier_dependent = false;
    last_qualifier_name = 0;
    if (at(preprocessing_op_or_punc::COLON_COLON)) {
        add_token();
        *scope = declared_names::global_namespace;
        qualified = true;
    } else if (at(standard_keyword::decltype_)) {
        uint64_t before = names.dependent_found;
        decltype_specifier();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::COLON_COLON))
            return expected("'::' after the decltype-specifier",
                            "expr.prim.id.qual");
        *known = false;
        qualifier_dependent = names.dependent_found != before;
        qualified = true;
    }
    for (;;) {
        /* pack-index-specifier :: (P2662R3, C++26): a later edition's */
        if (at_identifier() && at(preprocessing_op_or_punc::ELLIPSIS, 1) &&
            at(preprocessing_op_or_punc::LEFT_BRACKET, 2))
            return beyond_this_step();
        bool arguments = false, template_keyword = false;
        unsigned length = nested_component_length(
            *this, qualified, *known, *scope, &arguments, &template_keyword);
        if (!length)
            break;
        if (template_keyword)
            add_token();
        uint32_t name = tokens.identifier(position);
        scope_index found = 0;
        name_kind kind = name_kind::not_declared;
        bool found_dependent = false;
        if (*known) {
            if (qualified)
                kind = names.lookup_qualified(*scope, name, &found,
                                              &found_dependent);
            else
                kind = names.lookup_unqualified(name, &found, &found_dependent);
        }
        bool in_class =
            qualified && *known && names.kind_of(*scope) == scope_kind::class_;
        size_t id_mark = begin_node();
        token_index id_first = position;
        add_token();
        bool arguments_dependent = false;
        if (arguments) {
            uint64_t before = names.dependent_found;
            template_id_rest(id_mark, id_first,
                             syntax_kind::simple_template_id);
            if (failed())
                return 0;
            arguments_dependent = names.dependent_found != before;
        }
        if (!*known) {
            /* [temp.res]/4: a name after a dependent nested-name-specifier
               "is implicitly assumed to name a type"; after another type
               the parser cannot look into it is unknown */
        } else if (kind == name_kind::namespace_name && !arguments) {
            *scope = found;
        } else if (kind == name_kind::template_name && arguments && found &&
                   names.kind_of(found) == scope_kind::class_) {
            /* a specialization of a class template: its members are looked
               up in the primary template, whose members the parser knows
               (the current instantiation, [temp.dep.type]/1, in an
               out-of-class member's declarator-id); one that depends on a
               template parameter is dependent outside the template */
            *scope = found;
            if (arguments_dependent && !names.encloses_current(found))
                qualifier_dependent = true;
        } else if ((kind == name_kind::type_name ||
                    kind == name_kind::template_name) &&
                   found && names.kind_of(found) == scope_kind::class_ &&
                   (!found_dependent || names.encloses_current(found))) {
            /* a class whose members the parser knows, or a class template's
               injected-class-name ([temp.local]/1) */
            *scope = found;
        } else if (kind == name_kind::type_name ||
                   kind == name_kind::template_name) {
            *known = false; /* an enumeration, a typedef-name, a
                               specialization */
            if (found_dependent || arguments_dependent)
                qualifier_dependent = true;
        } else if ((kind == name_kind::not_declared && in_class) ||
                   template_keyword) {
            /* a member of a base the parser does not know */
            *known = false;
        } else {
            /* [basic.lookup.qual]/1: "If the name found does not
               designate a namespace or a class, enumeration, or dependent
               type, the program is ill-formed": a semantic rule; a name
               declared where the parser cannot see it is the same to it */
            return beyond_this_step();
        }
        last_qualifier_name = name;
        add_token(); /* :: */
        qualified = true;
    }
    if (position == first)
        return 0;
    if (qualifier_dependent)
        names.dependent_found++;
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
    unsigned end = 0;
    if (member && at(standard_keyword::template_) && at_identifier(1) &&
        at(preprocessing_op_or_punc::LESS, 2) && (end = angle_group_end(2)) &&
        at(preprocessing_op_or_punc::COLON_COLON, end)) {
        /* . template simple-template-id :: unqualified-id: a member of a
           class template's specialization named in the object's class */
        size_t qualifier_mark = begin_node();
        token_index qualifier_first = position;
        add_token();
        size_t id_mark = begin_node();
        token_index id_first = position;
        add_token();
        template_id_rest(id_mark, id_first, syntax_kind::simple_template_id);
        if (failed())
            return 0;
        add_token(); /* :: */
        qualifier = finish_node(syntax_kind::nested_name_specifier,
                                qualifier_mark, qualifier_first);
        known = false;
        qualifier_dependent = false;
    } else if (nested_name_specifier_follows(0) ||
               at(standard_keyword::decltype_)) {
        qualifier = nested_name_specifier(&scope, &known, false);
        if (failed())
            return 0;
    }
    /* [temp.names]/4: "A < is interpreted as the delimiter of a
       template-argument-list if it follows a name that is not a
       conversion-function-id and ... that is preceded by the keyword
       template" */
    bool template_keyword = false;
    if (at(standard_keyword::template_)) {
        if (!qualifier && !member)
            return expected("an id-expression", "expr.prim.id");
        add_token();
        template_keyword = true;
        if (!at_identifier() && !at(standard_keyword::operator_))
            return expected("a template-id after template", "temp.names");
    }
    bool dependent_qualifier = qualifier && qualifier_dependent;
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
            size_t id_mark = begin_node();
            token_index id_first = position;
            add_token();
            if (at(preprocessing_op_or_punc::LESS)) {
                /* ~ simple-template-id names the destructor of a
                   specialization ([class.dtor]/1) */
                template_id_rest(id_mark, id_first,
                                 syntax_kind::simple_template_id);
                if (failed())
                    return 0;
            }
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
        size_t id_mark = begin_node();
        token_index id_first = position;
        add_token();
        if (kind == name_kind::not_declared && !template_keyword)
            unknown_name = true;
        if (at(preprocessing_op_or_punc::LESS)) {
            if (template_keyword ||
                (known && !member && is_template_kind(kind))) {
                template_id_rest(id_mark, id_first,
                                 syntax_kind::simple_template_id);
                if (failed())
                    return 0;
            } else if (known && !member && !qualifier &&
                       names_template(kind, false, false)) {
                /* [temp.names]/2: "A name is also considered to refer to a
                   template if it is an unqualified-id followed by a < and
                   name lookup either finds one or more functions or finds
                   nothing" (P0846R0, C++20) */
                template_id_rest(id_mark, id_first,
                                 syntax_kind::simple_template_id);
                if (failed())
                    return 0;
            } else if ((member || (!known && !dependent_qualifier)) &&
                       !dependent_qualifier) {
                /* a member's name and a name of a class the parser does not
                   know may be a template's: the tokens are a template-id
                   when they parse as one and a call or a '::' follows */
                tentative_parse attempt(*this);
                template_id_rest(id_mark, id_first,
                                 syntax_kind::simple_template_id);
                if (!failed() &&
                    (at(preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
                     at(preprocessing_op_or_punc::COLON_COLON))) {
                    attempt.commit();
                    unknown_name = false;
                }
            }
        }
    } else if (template_keyword) {
        return expected("a template-id after template", "temp.names");
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
    /* [dcl.decl]/4: declarator-id: ..._opt id-expression, a function
       parameter pack ([temp.variadic]/3) */
    if (at(preprocessing_op_or_punc::ELLIPSIS)) {
        if (!tokens.files.behavior.has(language_feature::variadic_templates))
            return beyond_this_step();
        add_token();
        facts.pack = true;
    }
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
    bool dependent_qualifier = qualifier && qualifier_dependent;
    uint32_t qualifier_name = last_qualifier_name;
    bool template_keyword = qualifier && accept(standard_keyword::template_);
    node_index unqualified = 0;
    size_t id_mark = begin_node();
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
    if (at(preprocessing_op_or_punc::LESS) && dependent_qualifier &&
        !template_keyword)
        return violates("template before a dependent template-name",
                        "temp.names", position - 1);
    /* [class.qual]/2: the name of a constructor after its class's
       nested-name-specifier names the constructor: no template-arguments */
    if (at(preprocessing_op_or_punc::LESS) && qualifier && !unqualified &&
        tokens[position - 1].kind == token_kind::identifier &&
        tokens.identifier(position - 1) == qualifier_name)
        return violates("no template-argument-list after a constructor's "
                        "name",
                        "class.qual", position);
    if (at(preprocessing_op_or_punc::LESS)) {
        /* a template-id: an explicit specialization's or a partial
           specialization's name ([temp.expl.spec], [temp.spec.partial]),
           which declares no new name */
        syntax_kind kind = syntax_kind::template_id;
        if (tokens[facts.id_first].kind == token_kind::identifier &&
            unqualified == 0)
            kind = syntax_kind::simple_template_id;
        template_id_rest(id_mark, facts.id_first, kind);
        if (failed())
            return 0;
        facts.template_id = true;
        facts.name = 0;
    }
    facts.id_last = position - 1;
    if (!qualifier)
        return 0; /* an unqualified-id passes through */
    facts.qualified_id = true;
    return finish_node(syntax_kind::qualified_id, mark, first);
}

/* ---- semantic analysis ---------------------------------------------- */

/*
 * [expr.prim.id.unqual]/2: "The result is the entity denoted by the
 * identifier. ... Otherwise, the type of the expression is the type of the
 * result. ... The expression is an lvalue if the
 * entity is a function, variable, structured binding, data member, or
 * template parameter object and a prvalue otherwise".  At global namespace
 * scope, unqualified lookup ([basic.lookup.unqual]) finds the declaration
 * of the name there before; a name it does not find may be one a
 * namespace or a using-directive analysis has no rules for declares.
 */
analysis_result semantic_analysis::id_expression(token_index t,
                                                 expression_meaning &m)
{
    entity_index e = lookup(tokens.identifier(t));
    if (!e || entity_table[e].kind == entity_kind::none)
        return not_supported(current_declaration, "basic.lookup.unqual");
    const entity &x = entity_table[e];
    if (x.kind != entity_kind::variable && x.kind != entity_kind::function)
        return not_supported(current_declaration, "expr.prim.id");
    m.category = value_category::lvalue;
    m.type = x.type;
    m.entity = e;
    /* the address of an object or function with static storage duration
       is a constant ([expr.const]/11) */
    m.evaluation = constness::constant;
    /* a variable with automatic storage duration has no constant address;
       reading one usable in constant expressions is still a constant
       ([expr.const]/3) */
    if (x.automatic)
        m.evaluation = constness::unknown;
    const struct type record = type_table[x.type.unqualified()];
    if (record.kind == type_kind::lvalue_reference ||
        record.kind == type_kind::rvalue_reference) {
        /* [expr.type]/1: "If an expression initially has the type
           "reference to T", the type is adjusted to T prior to any further
           analysis." */
        m.type = record.operand;
        m.entity = 0;
        /* [expr.const]/5.12: a reference is usable only with "a preceding
           initialization": not modelled */
        m.evaluation = constness::unknown;
    }
    return analysis_result::well_formed;
}

} // namespace cplusplus
