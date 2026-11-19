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
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::using_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* using */
    if (!comma_list(
            syntax_kind::using_declarator_list, [this] { using_declarator(); },
            [this] {
                /* GNU attributes after a using-declarator (clang's
                   extension; libc++'s using_if_exists) */
                if (at_gnu_attribute())
                    gnu_attributes_opt();
                /* an attribute-specifier-seq after a
                   using-declarator: clang's extension, P9's */
                else if (at_attribute_specifier())
                    beyond_this_step();
            }))
        return 0;
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the using-declaration", "namespace.udecl");
    return finish_node(syntax_kind::using_declaration, mark, first);
}

/*
 *     using-declarator:
 *         typename_opt nested-name-specifier unqualified-id
 *
 * The unqualified-ids that are operator names are over.oper.cpp's.
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
    /* unqualified-id: no template keyword before it */
    if (at(standard_keyword::template_))
        return expected("an unqualified-id after the nested-name-specifier",
                        "namespace.udecl");
    uint32_t name = 0;
    if (at(standard_keyword::operator_)) {
        operator_unqualified_id();
        if (failed())
            return 0;
    } else if (at(preprocessing_op_or_punc::TILDE)) {
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
    /* [namespace.udecl]/5: "A using-declarator shall not name a
       template-id." */
    if (at(preprocessing_op_or_punc::LESS))
        return violates("no template-id in a using-declarator",
                        "namespace.udecl", position);
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

/* ---- semantic analysis ---------------------------------------------- */

/*
 * [namespace.udecl]/1: "Each using-declarator in a using-declaration
 * introduces a set of declarations into the declarative region in which
 * the using-declaration appears.  The set of declarations introduced by
 * the using-declarator is found by performing qualified name lookup
 * ([basic.lookup.qual], [class.member.lookup]) for the name in the
 * using-declarator, excluding functions that are hidden as described
 * below."  /2: "Every using-declaration is a declaration and a
 * member-declaration and can therefore be used in a class definition."
 */
analysis_result semantic_analysis::using_declaration(UsingDeclaration n)
{
    /* the using-declarators, the GNU attributes after each (clang's
       extension) and the commas: of the using-declaration, or of its
       using-declarator-list */
    auto node = syntax(n);
    owned_array<SyntaxElement> items;
    for (auto c : node.elements()) {
        if (syntax(c).is(syntax_kind::using_declarator_list)) {
            for (auto child : syntax(c).elements())
                items.append(child);
        } else if (!c.is_token()) {
            items.append(c);
        }
    }
    analysis_result result = analysis_result::well_formed;
    bool any = false;
    for (size_t i = 0; i < items.size(); i++) {
        SyntaxElement d = items[i];
        if (syntax(d).is(syntax_kind::using_declarator)) {
            gnu_attributes g;
            for (size_t k = i + 1;
                 k < items.size() &&
                 syntax(items[k]).is(syntax_kind::gnu_attribute_specifier);
                 k++) {
                analysis_result r =
                    gnu_attribute_specifier(items[k].index(), g);
                if (r != analysis_result::well_formed)
                    return worse(result, r);
            }
            if ((g.flags & ~gnu_using_if_exists) || g.aligned || g.section)
                return worse(result, not_supported(n, "namespace.udecl"));
            result = worse(result,
                           using_declarator(d, g.flags & gnu_using_if_exists));
            any = true;
        } else if (syntax(d).is(syntax_kind::gnu_attribute_specifier)) {
            continue; /* the using-declarator's before it */
        } else if (!syntax(d).is(preprocessing_op_or_punc::COMMA)) {
            /* a pack expansion */
            result = worse(result, not_supported(n, "temp.variadic"));
        }
    }
    if (!any)
        result = worse(result, not_supported(n, "namespace.udecl"));
    return result;
}

/*
 *     using-declarator:
 *         typename_opt nested-name-specifier unqualified-id
 *
 * The name is bound in the scope of the using-declaration to the entity it
 * names ([namespace.udecl]/1), marked as bound by a using-declaration: a
 * declaration of the name there does not redeclare the entity
 * ([dcl.meaning]/1: "the member shall not merely have been introduced by a
 * using-declaration").  A set of more than one declaration (overloaded
 * functions, or a class and what hides it found in two namespaces) has no
 * rules yet.
 */
analysis_result semantic_analysis::using_declarator(UsingDeclarator n,
                                                    bool if_exists)
{
    auto node = syntax(n);
    if (node.size() != 2 || !node[0].is(syntax_kind::nested_name_specifier) ||
        !node[1].is_identifier())
        /* typename, an operator-function-id, a destructor's name, a pack
           expansion */
        return not_supported(n, "namespace.udecl");
    nominated_scope q;
    analysis_result r = nested_name_specifier(node[0], q);
    if (r != analysis_result::well_formed)
        return r;
    token_index t = node[1].token();
    uint32_t name = tokens.identifier(t);
    lookup_result found = lookup_qualified(q.entity, name, lookup_filter::any);
    if (found.unknown)
        return not_supported(n, "over");
    /* clang's using_if_exists: "the using-declaration is ignored if the
       name is not found" (a name an unsupported declaration declares
       is not found either, so this trusts the library's headers) */
    if (if_exists && !found.ambiguous && !found.entity)
        return analysis_result::well_formed;
    /* the declarations found in more than one namespace, or none (which
       may be ones analysis has no rules for) */
    if (found.ambiguous || !found.entity)
        return not_supported(n, "namespace.udecl");
    entity_index e = found.entity;
    entity_kind kind = entity_table[e].kind;
    bool names_class_member =
        !is_namespace(q.entity) &&
        entity_table[q.entity].kind == entity_kind::class_type;
    if (!is_namespace(q.entity) && !names_class_member)
        /* an enumerator named through its enumeration (C++20) */
        return not_supported(n, "namespace.udecl");
    if (defining_class) {
        entity_index c = defining_class;
        /* [namespace.udecl]/3: "In a using-declaration used as a
           member-declaration, each using-declarator shall either name an
           enumerator or have a nested-name-specifier naming a base class
           of the class being defined." */
        /* (an enumerator of a namespace's enumeration: C++20) */
        if (is_namespace(q.entity) &&
            (kind == entity_kind::enumerator || kind == entity_kind::none))
            return not_supported(n, "namespace.udecl");
        if (is_namespace(q.entity))
            return ill_formed_at_token(t,
                                       "in a using-declaration used as a "
                                       "member-declaration, each "
                                       "using-declarator shall have a "
                                       "nested-name-specifier naming a base "
                                       "class of the class being defined",
                                       "namespace.udecl");
        bool ambiguous = false;
        if (!base_step(c, q.entity, ambiguous))
            return not_supported(n, "namespace.udecl");
        /* [namespace.udecl]/3: "If a using-declarator names a
           constructor, its nested-name-specifier shall name a direct base
           class of the class being defined": inheriting constructors */
        if (name == entity_table[q.entity].name)
            return not_supported(n, "class.inhctor.init");
        /* [namespace.udecl]/19: "A synonym created by a using-declaration
           has the usual accessibility for a member-declaration."  Access
           is the entity's here, so only a declaration that leaves it as
           it is has rules; a name the derived class declares too would be
           an overload or hide the base's */
        if (kind == entity_kind::none || entity_table.find(c, name) ||
            entity_table[e].access != declaration_access)
            return not_supported(n, "namespace.udecl");
        entity_table.bind(c, name, e, true);
        used(t, e);
        return analysis_result::well_formed;
    }
    if (names_class_member) {
        /* [namespace.udecl]/7: "A using-declaration that names a class
           member other than an enumerator shall be a member-declaration." */
        if (kind == entity_kind::enumerator || kind == entity_kind::none)
            return not_supported(n, "namespace.udecl");
        return ill_formed_at_token(t,
                                   "a using-declaration that names a class "
                                   "member other than an enumerator shall "
                                   "be a member-declaration",
                                   "namespace.udecl");
    }
    /* [namespace.udecl]/6: "A using-declaration shall not name a
       namespace." */
    if (kind == entity_kind::namespace_entity)
        return ill_formed_at_token(t,
                                   "a using-declaration shall not name a "
                                   "namespace",
                                   "namespace.udecl");
    entity_index scope = current_scope;
    entity_index prior = entity_table.find(scope, name);
    if (kind == entity_kind::function)
        entity_table[e].named_by_using = true;
    if (!prior) {
        entity_table.bind(scope, name, e, true);
        used(t, e);
        return analysis_result::well_formed;
    }
    entity_kind prior_kind = entity_table[prior].kind;
    if (prior_kind == entity_kind::none)
        return not_supported(n, "basic.scope.declarative");
    if (prior == e) {
        /* [namespace.udecl]/9: "A using-declaration is a declaration and can
           therefore be used repeatedly where (and only where) multiple
           declarations are allowed." */
        /* (in a block too: clang, as CWG issue 36 was resolved) */
        if (is_namespace(scope) || entity_table.bound_by_using(scope, name)) {
            used(t, e);
            return analysis_result::well_formed;
        }
        return not_supported(n, "namespace.udecl");
    }
    /* [namespace.udecl]/12: "Since a using-declaration is a declaration, the
       restrictions on declarations of the same name in the same declarative
       region also apply to using-declarations." Two functions are overloads; a
       class or enumeration and a variable or function hide one another; a
       typedef-name may name the same type */
    if (kind == entity_kind::function && prior_kind == entity_kind::function)
        return not_supported(n, "over");
    bool plain =
        (kind == entity_kind::variable || kind == entity_kind::function) &&
        (prior_kind == entity_kind::variable ||
         prior_kind == entity_kind::function);
    if (!plain || entity_table[e].hidden_type ||
        entity_table[prior].hidden_type)
        return not_supported(n, "namespace.udecl");
    /* [basic.scope.declarative]/4: "Given a set of declarations in a
       single declarative region, each of which specifies the same
       unqualified name, they shall all refer to the same entity, or all
       refer to functions and function templates" */
    return ill_formed_at_token(t,
                               "declarations of the same name in a "
                               "declarative region shall all refer to the "
                               "same entity, or all to functions",
                               "basic.scope.declarative");
}

} // namespace cplusplus
