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
#include "frontend/cplusplus/sema.h"

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
    /* [ext.gnu] GCC's attributes of a namespace follow its name
       (namespace std __attribute__ ((__visibility__ ("default"))) { ...) */
    if (kind == syntax_kind::nested_namespace_definition && at_gnu_attribute())
        return violates("no attributes on a nested-namespace-definition",
                        "ext.gnu", position);
    gnu_attributes_opt();
    if (failed())
        return 0;
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

/* ---- semantic analysis ---------------------------------------------- */

/*
 * [namespace.def]/2: "In a named-namespace-definition, the identifier is
 * the name of the namespace.  If the identifier, when looked up, refers to
 * a namespace-name (but not a namespace-alias) that was introduced in the
 * namespace in which the named-namespace-definition appears or that was
 * introduced in a member of the inline namespace set of that namespace, the
 * namespace-definition extends the previously-declared namespace.
 * Otherwise, the identifier is introduced as a namespace-name into the
 * declarative region in which the named-namespace-definition appears."
 * /9: "A nested-namespace-definition with an enclosing-namespace-specifier
 * E, identifier I and namespace-body B is equivalent to" namespace E {
 * inline_opt namespace I { B } } "where the optional inline is present if
 * and only if the identifier I is preceded by inline."
 *
 * [namespace.def]/5: "The inline keyword may be used on a
 * namespace-definition that extends a namespace only if it was previously
 * used on the namespace-definition that initially declared the
 * namespace-name for that namespace."  /7: "Members of an inline namespace
 * can be used in most respects as though they were members of the enclosing
 * namespace.  Specifically, ... a using-directive that names the inline
 * namespace is implicitly inserted into the enclosing namespace as for an
 * unnamed namespace".
 *
 * [namespace.unnamed]/1: "An unnamed-namespace-definition behaves as if it
 * were replaced by"
 *     inline_opt namespace unique { }
 *     using namespace unique ;
 *     namespace unique { namespace-body }
 * "where inline appears if and only if it appears in the
 * unnamed-namespace-definition and all occurrences of unique in a
 * translation unit are replaced by the same identifier, and this identifier
 * differs from all other identifiers in the translation unit."
 *
 * The declarations of the namespace-body are analysed one by one, each a
 * declaration of its own in semantic_analysis::declarations: the definition
 * itself has a record only when it is not well-formed.
 */
analysis_result semantic_analysis::namespace_definition(SyntaxRef n)
{
    auto node = n;
    /* [namespace.def]/1 (the grammar): a namespace-definition is a
       declaration of a namespace-body or the translation unit */
    if (!is_namespace(current_scope) || defining_class || function_state.active)
        return not_supported(n, "namespace.def");
    struct component {
        uint32_t name = 0;
        token_index at = 0;
        bool inline_ = false;
    };
    owned_array<component> components;
    bool inline_next = false;
    bool body = false;
    /* the tokens before the '{': inline, namespace, the names and ::, and
       those of an enclosing-namespace-specifier */
    owned_array<SyntaxElement> head;
    for (uint32_t i = 0; i < node.size() && !body; i++) {
        SyntaxElement c = node[i];
        if (c.is(syntax_kind::enclosing_namespace_specifier)) {
            for (auto child : c.elements())
                head.append(child);
            continue;
        }
        if (c.is(syntax_kind::gnu_attribute_specifier)) {
            /* GNU attributes that change nothing a namespace's members
               get (abi_tag, which changes their symbols, has no rule) */
            gnu_attributes g;
            analysis_result r = gnu_attribute_specifier(c.index(), g);
            if (r != analysis_result::well_formed)
                return r;
            if (g.flags || g.aligned || g.section)
                return not_supported(n, "dcl.attr");
            continue;
        }
        if (!c.is_token())
            /* an attribute-specifier-seq */
            return not_supported(n, "dcl.attr");
        if (c.is(preprocessing_op_or_punc::LEFT_BRACE))
            body = true;
        else
            head.append(c);
    }
    if (!body)
        return not_supported(n, "namespace.def");
    for (SyntaxElement c : head) {
        if (!c.is_token())
            return not_supported(n, "namespace.def");
        const token &t = tokens[c.token()];
        if (t.kind == token_kind::identifier) {
            component x;
            x.name = tokens.identifier(c.token());
            x.at = c.token();
            x.inline_ = inline_next;
            inline_next = false;
            components.append(x);
            continue;
        }
        if (p.keyword_of(t) == standard_keyword::inline_)
            inline_next = true;
    }
    if (components.empty()) {
        component unnamed;
        unnamed.at = node.first_token();
        unnamed.inline_ = inline_next;
        components.append(unnamed);
    }
    entity_index scope = current_scope;
    for (const component &x : components) {
        entity_index e = 0;
        if (x.name) {
            /* the name looked up in the namespace and its inline namespace
               set */
            lookup_result found =
                lookup_in_namespace(scope, x.name, lookup_filter::any);
            e = entity_table.find(scope, x.name);
            if (!e && found.entity &&
                entity_table[found.entity].kind ==
                    entity_kind::namespace_entity &&
                entity_table[found.entity].name == x.name) {
                /* introduced in a member of the inline namespace set: its
                   namespace is reached from scope through inline
                   namespaces alone */
                entity_index y = entity_table[found.entity].enclosing;
                while (y != scope && y && entity_table[y].inline_)
                    y = entity_table[y].enclosing;
                if (y == scope)
                    e = found.entity;
            }
        } else {
            for (const using_directive_record &u : using_directives)
                if (u.scope == scope && !entity_table[u.nominated].name &&
                    entity_table[u.nominated].enclosing == scope)
                    e = u.nominated;
        }
        if (e) {
            const entity &prior = entity_table[e];
            if (prior.kind == entity_kind::none)
                return not_supported(n, "basic.scope.declarative");
            /* [basic.scope.declarative]/4: "Given a set of declarations in
               a single declarative region, each of which specifies the same
               unqualified name, they shall all refer to the same entity, or
               all refer to functions and function templates": the name is
               something else's, or a namespace-alias ([namespace.def]/2:
               "but not a namespace-alias") */
            if (prior.kind != entity_kind::namespace_entity ||
                prior.name != x.name)
                return ill_formed_at_token(
                    x.at,
                    "declarations of the same name in a declarative region "
                    "shall all refer to the same entity, or all to functions",
                    "basic.scope.declarative");
            /* [namespace.def]/5 */
            if (x.inline_ && !prior.inline_)
                return ill_formed_at_token(
                    x.at,
                    "the inline keyword may be used on a "
                    "namespace-definition that extends a namespace only if "
                    "it was previously used on the namespace-definition "
                    "that initially declared the namespace-name",
                    "namespace.def");
        } else {
            e = entity_table.make(entity_kind::namespace_entity, x.name, scope);
            entity &made = entity_table[e];
            made.inline_ = x.inline_;
            made.defined = true;
            /* [basic.link]/4: "An unnamed namespace or a namespace declared
               directly or indirectly within an unnamed namespace has
               internal linkage.  All other namespaces have external
               linkage." */
            made.linkage_of_name = linkage::external;
            if (within_unnamed_namespace(e))
                made.linkage_of_name = linkage::internal;
            if (x.name)
                entity_table.bind(scope, x.name, e);
            if (!x.name || x.inline_) {
                using_directive_record u;
                u.scope = scope;
                u.nominated = e;
                using_directives.append(u);
                entity_table[scope].has_using_directive = true;
            }
        }
        entity_table.add_declaration(e, n);
        scope = e;
    }
    entity_index outer = current_scope;
    entity_index outer_claims = claim_scope;
    current_scope = scope;
    /* claims about the expressions of a declaration are placed after it,
       in its namespace */
    claim_scope = scope;
    declaration_seq_in(n);
    current_scope = outer;
    claim_scope = outer_claims;
    current_declaration = n;
    return analysis_result::well_formed;
}

/*
 * [basic.lookup.udir]/1: "In a using-directive or
 * namespace-alias-definition, during the lookup for a namespace-name or for
 * a name in a nested-name-specifier only namespace names are considered."
 * The namespace the name denotes ([namespace.alias]/2: a namespace-alias
 * is "a synonym for the name of the namespace" it was defined to).
 */
analysis_result semantic_analysis::named_namespace(node_index at,
                                                   SyntaxRef qualifier,
                                                   token_index name,
                                                   entity_index &n)
{
    lookup_result found;
    if (qualifier) {
        nominated_scope scope;
        analysis_result r = nested_name_specifier(qualifier, scope);
        if (r != analysis_result::well_formed)
            return r;
        if (!is_namespace(scope.entity))
            return not_supported(at, "basic.lookup.udir");
        found = lookup_in_namespace(scope.entity, tokens.identifier(name),
                                    lookup_filter::namespaces);
    } else {
        found = lookup_unqualified(tokens.identifier(name),
                                   lookup_filter::namespaces);
    }
    if (found.ambiguous)
        return ill_formed_at_token(name,
                                   "a name found in two different namespaces "
                                   "whose declarations do not declare the "
                                   "same entity is ill-formed",
                                   "namespace.udir");
    /* one not found may be a namespace a declaration analysis has no
       rules for declares */
    if (!found.entity ||
        entity_table[found.entity].kind != entity_kind::namespace_entity)
        return not_supported(at, "basic.lookup.udir");
    used(name, found.entity);
    n = found.entity;
    return analysis_result::well_formed;
}

/*
 * [namespace.udir]/1: "A using-directive shall not appear in class scope,
 * but may appear in namespace scope or in block scope."  /2: "A
 * using-directive specifies that the names in the nominated namespace can
 * be used in the scope in which the using-directive appears after the
 * using-directive."  /3: "A using-directive does not add any members to
 * the declarative region in which it appears."  The directive is recorded
 * with its scope; unqualified and qualified lookup use it
 * (basic/basic.lookup.cpp).
 */
analysis_result semantic_analysis::using_directive(UsingDirective n)
{
    auto node = n;
    entity_index scope = current_scope;
    /* until the directive is analysed, what it makes visible is not
       known */
    auto unknown = [&](analysis_result r) {
        if (r != analysis_result::well_formed)
            entity_table[scope].unknown_names = true;
        return r;
    };
    if (defining_class)
        return unknown(not_supported(n, "namespace.udir"));
    node_index qualifier = 0;
    token_index name = 0;
    for (auto c : node.elements()) {
        if (c.is(syntax_kind::nested_name_specifier)) {
            qualifier = c.index();
            continue;
        }
        if (!c.is_token())
            /* an attribute-specifier-seq */
            return unknown(not_supported(n, "dcl.attr"));
        if (tokens[c.token()].kind == token_kind::identifier)
            name = c.token();
    }
    if (!name)
        return unknown(not_supported(n, "namespace.udir"));
    entity_index nominated = 0;
    analysis_result r = named_namespace(n, syntax(qualifier), name, nominated);
    if (r != analysis_result::well_formed)
        return unknown(r);
    for (const using_directive_record &u : using_directives)
        if (u.scope == scope && u.nominated == nominated)
            return analysis_result::well_formed;
    using_directive_record u;
    u.scope = scope;
    u.nominated = nominated;
    using_directives.append(u);
    entity_table[scope].has_using_directive = true;
    return analysis_result::well_formed;
}

/*
 * [namespace.alias]/1: "A namespace-alias-definition declares an alternate
 * name for a namespace"; /2: "The identifier in a
 * namespace-alias-definition is a synonym for the name of the namespace
 * denoted by the qualified-namespace-specifier and becomes a
 * namespace-alias."  /3: "In a declarative region, a
 * namespace-alias-definition can be used to redefine a namespace-alias
 * declared in that declarative region to refer only to the namespace to
 * which it already refers."  A name of the region that is something else's
 * is [basic.scope.declarative]/4's.  The alias is the namespace bound under
 * another name in the scope.
 */
analysis_result
semantic_analysis::namespace_alias_definition(NamespaceAliasDefinition n)
{
    auto node = n;
    if (defining_class || node.size() < 4 || !node[1].is_identifier())
        return not_supported(n, "namespace.alias");
    token_index alias = node[1].token();
    node_index qualifier = 0;
    token_index name = 0;
    SyntaxElement named = node[3];
    if (named.is(syntax_kind::qualified_namespace_specifier)) {
        auto parts = named;
        if (named.size() != 2 ||
            !parts[0].is(syntax_kind::nested_name_specifier) ||
            !parts[1].is_token())
            return not_supported(n, "namespace.alias");
        qualifier = parts[0].index();
        name = parts[1].token();
    } else if (named.is_token() &&
               tokens[named.token()].kind == token_kind::identifier) {
        name = named.token();
    } else {
        return not_supported(n, "namespace.alias");
    }
    entity_index nominated = 0;
    analysis_result r = named_namespace(n, syntax(qualifier), name, nominated);
    if (r != analysis_result::well_formed)
        return r;
    entity_index scope = current_scope;
    uint32_t alias_name = tokens.identifier(alias);
    entity_index prior = entity_table.find(scope, alias_name);
    if (prior) {
        if (entity_table[prior].kind == entity_kind::none)
            return not_supported(n, "basic.scope.declarative");
        if (prior == nominated)
            /* /3: the alias again; (or the namespace's own name in its
               scope, which clang accepts too) */
            return analysis_result::well_formed;
        /* [basic.scope.declarative]/4: "they shall all refer to the same
           entity, or all refer to functions and function templates" */
        return ill_formed_at_token(alias,
                                   "declarations of the same name in a "
                                   "declarative region shall all refer to "
                                   "the same entity, or all to functions",
                                   "basic.scope.declarative");
    }
    entity_table.bind(scope, alias_name, nominated);
    return analysis_result::well_formed;
}

/* the declarations between the braces of a namespace-definition */
void semantic_analysis::declaration_seq_in(SyntaxRef container)
{
    auto namespace_body = container;
    for (auto child : namespace_body.elements())
        for (auto d : child.declaration_seq().declarations())
            declaration(d);
}

/* [namespace.std]/1: "the namespace std" of the global namespace */
bool semantic_analysis::std_namespace(entity_index scope) const
{
    const entity &n = entity_table[scope];
    return scope && n.kind == entity_kind::namespace_entity && !n.enclosing &&
           named(scope, "std");
}

bool semantic_analysis::named(entity_index e, const char *text) const
{
    if (!entity_table[e].name)
        return false;
    size_t length = 0;
    const char *spelling =
        tokens.pp.identifier_spelling(entity_table[e].name - 1, &length);
    return length == strlen(text) && !memcmp(spelling, text, length);
}

} // namespace cplusplus
