/*
 * using-declaration.cpp: [namespace.udecl] The using declaration.
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
 * The unqualified-ids that are operator names are operator-function.cpp's.
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
        scope_index owned = 0;
        bool dependent = false;
        if (typename_) {
            kind = name_kind::type_name;
            /* [temp.dep.type]/9: a type named through a dependent
               nested-name-specifier is dependent */
            dependent = qualifier_dependent;
        } else if (known) {
            kind = names.lookup_qualified(scope, name, &owned, &dependent);
        } else if (qualifier_dependent) {
            /* [temp.dep.type]/6, [temp.res]/4: what a dependent base's
               member is, the parser cannot tell, but it names no type
               without typename: it is declared as something that is no
               type, as a call of it in a member's body needs */
            kind = name_kind::other;
            dependent = true;
        }
        if (kind != name_kind::not_declared &&
            kind != name_kind::namespace_name)
            names.declare(name, kind, owned, dependent);
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
    owned_array<SyntaxElement> items;
    for (auto c : n.elements()) {
        if (c.is(syntax_kind::using_declarator_list)) {
            for (auto child : c.elements())
                items.append(child);
        } else if (!c.is_token()) {
            items.append(c);
        }
    }
    analysis_result result = analysis_result::well_formed;
    bool any = false;
    for (size_t i = 0; i < items.size(); i++) {
        SyntaxElement d = items[i];
        if (d.is(syntax_kind::using_declarator)) {
            gnu_attributes g;
            for (size_t k = i + 1;
                 k < items.size() &&
                 items[k].is(syntax_kind::gnu_attribute_specifier);
                 k++) {
                analysis_result r = gnu_attribute_specifier(items[k], g);
                if (r != analysis_result::well_formed)
                    return worse(result, r);
            }
            if ((g.flags & ~gnu_using_if_exists) || g.aligned || g.section)
                return worse(result, not_supported_because(
                                         n, "namespace.udecl",
                                         "a using-declaration attribute other "
                                         "than using_if_exists"));
            size_t after = i + 1;
            while (after < items.size() &&
                   items[after].is(syntax_kind::gnu_attribute_specifier))
                after++;
            if (after < items.size() &&
                items[after].is(preprocessing_op_or_punc::ELLIPSIS))
                result = worse(result, using_declarator_pack(
                                           d, g.flags & gnu_using_if_exists));
            else
                result = worse(
                    result, using_declarator(d, g.flags & gnu_using_if_exists));
            any = true;
        } else if (d.is(syntax_kind::gnu_attribute_specifier) ||
                   d.is(preprocessing_op_or_punc::ELLIPSIS)) {
            continue; /* the using-declarator's before it */
        } else if (!d.is(preprocessing_op_or_punc::COMMA)) {
            /* a pack expansion */
            result = worse(
                result, not_supported_because(
                            n, "temp.variadic",
                            "an unexpected part in a using-declarator-list"));
        }
    }
    if (!any)
        result = worse(result, not_supported_because(
                                   n, "namespace.udecl",
                                   "a using-declaration with no declarator"));
    return result;
}

/*
 * [temp.variadic]/5: "In a using-declaration; the pattern is a
 * using-declarator", /7: "The instantiation of a pack expansion ... produces
 * a list E1, ..., EN": the using-declarator, once with each element of the
 * packs it names (`using Ts::operator()...;`).  In a template's own
 * definition the packs are not known.
 */
analysis_result semantic_analysis::using_declarator_pack(UsingDeclarator n,
                                                         bool if_exists)
{
    uint32_t count = 0;
    if (!expansion_count(n.first_token(), n.end_token(), count))
        return not_supported_because(
            n, "temp.variadic",
            "a using pack whose expansion count is unknown");
    analysis_result result = analysis_result::well_formed;
    for (uint32_t e = 0; e < count; e++) {
        expansion_scope scope(*this, true, e);
        result = worse(result, using_declarator(n, if_exists));
        if (result != analysis_result::well_formed)
            break;
    }
    return result;
}

/*
 * [namespace.udecl]/1: "the set of declarations introduced by the
 * using-declarator"; /15 (C++20 /14): a derived class's functions hide the
 * base's with their signature.  The chain of a class's name ends with the
 * functions of the base its first using-declaration named, which the
 * base's own chain shares; another base's functions cannot follow them
 * without changing the first base's chain.  The class's chain then links a
 * copy of each function a using-declaration introduced (using_shadow_of),
 * which overload_set takes for the function itself.  Two functions of
 * different bases with one signature conflict ([namespace.udecl]/14: the
 * program is ill-formed); virtual functions, member function templates and
 * the special members have no rules here.
 */
analysis_result
semantic_analysis::using_join_chains(UsingDeclarator n, token_index t,
                                     entity_index c, uint32_t name,
                                     entity_index head, entity_index e)
{
    auto plain = [&](entity_index f) {
        const entity &x = entity_table[f];
        return x.kind == entity_kind::function && !x.virtual_ &&
               !x.member_templates && !x.conversion_function &&
               x.special == special_member::none;
    };
    auto same_signature = [&](entity_index a, entity_index b) {
        return entity_table[a].member_cv == entity_table[b].member_cv &&
               same_parameters(type_table, entity_table[a].type.unqualified(),
                               entity_table[b].type.unqualified());
    };
    /* the class's own functions, which come first in the chain, and the
       copies at its end if it has any */
    owned_array<entity_index> chain;
    for (entity_index o = head; o; o = entity_table[o].next_overload)
        chain.append(o);
    size_t own_count = 0;
    while (own_count < chain.size() &&
           entity_table[chain[own_count]].enclosing == c &&
           !entity_table[chain[own_count]].using_shadow_of)
        own_count++;
    bool copies = own_count < chain.size() &&
                  entity_table[chain[own_count]].using_shadow_of;
    owned_array<entity_index> visible;
    overload_set(head, visible);
    owned_array<entity_index> present; /* what the base's functions are */
    for (entity_index f : visible) {
        if (entity_table[f].enclosing == c)
            continue;
        if (!plain(f))
            return not_supported_because(
                n, "namespace.udecl",
                "a base function the using-declaration cannot merge");
        present.append(f);
    }
    if (!plain(e))
        return not_supported_because(
            n, "namespace.udecl",
            "a named function the using-declaration cannot merge");
    owned_array<entity_index> introduced;
    owned_array<entity_index> found;
    overload_set(e, found);
    for (entity_index f : found) {
        if (!plain(f))
            return not_supported_because(
                n, "namespace.udecl",
                "an introduced function the using-declaration cannot merge");
        bool skip = false;
        for (entity_index o : visible) {
            if (o == f) {
                skip = true; /* /9: introduced already */
            } else if (same_signature(o, f)) {
                if (entity_table[o].enclosing == c)
                    skip = true; /* /15: hidden by the class's own */
                else if (!skip)
                    return not_supported_because(
                        n, "namespace.udecl",
                        "a base function with the signature of an earlier "
                        "using");
            }
        }
        for (entity_index o : introduced)
            if (same_signature(o, f))
                return not_supported_because(
                    n, "namespace.udecl",
                    "two introduced functions with the same signature");
        if (!skip)
            introduced.append(f);
    }
    auto shadow = [&](entity_index f) {
        entity copy = entity_table[f];
        copy.next_overload = 0;
        copy.using_shadow_of = f;
        entity_index s =
            entity_table.make(entity_kind::function, name, copy.enclosing);
        entity_table[s] = copy;
        return s;
    };
    entity_index last = 0;
    if (copies) {
        last = chain[chain.size() - 1];
    } else {
        /* the class's own, then a copy of each function of the bases */
        if (own_count)
            last = chain[own_count - 1];
        for (entity_index f : present) {
            entity_index s = shadow(f);
            if (last)
                entity_table[last].next_overload = s;
            else
                entity_table.bind(c, name, s, true);
            last = s;
        }
    }
    for (entity_index f : introduced) {
        entity_index s = shadow(f);
        entity_table[last].next_overload = s;
        last = s;
    }
    used(t, e);
    return analysis_result::well_formed;
}

/*
 *     using-declarator:
 *         typename_opt nested-name-specifier unqualified-id
 *
 * The name is bound in the scope of the using-declaration to the entity it
 * names ([namespace.udecl]/1), marked as bound by a using-declaration: a
 * declaration of the name there does not redeclare the entity
 * ([dcl.meaning]/1: "the member shall not merely have been introduced by a
 * using-declaration").  A set of overloaded functions and function templates
 * of a namespace is followed; other sets of more than one declaration (a
 * class and what hides it found in two namespaces) have no rules yet.
 */
/*
 * [namespace.udecl]/19: a using-declarator with typename names a type
 * ([temp.res]/5): "the name introduced is a synonym" for it, here a typedef
 * of the type typename Q::name has (a dependent one when Q is dependent,
 * [temp.dep.type]/9).
 */
analysis_result semantic_analysis::using_typename(UsingDeclarator n)
{
    type_index type = 0;
    uint32_t cv = 0;
    if (!defining_class)
        return not_supported_because(
            n, "namespace.udecl",
            "a using typename outside a class definition");
    VTRY(r, typename_type(n, type, cv));
    if (!type)
        return not_supported_because(n, "namespace.udecl",
                                     "a using typename that names no type");
    qualified_type q;
    q = qualified_type::make(type, cv);
    specified s;
    s.typedef_ = true;
    s.type = q;
    s.has_type = true;
    return member_typedef(defining_class, n, n[2].token(), q, s,
                          declarator_meaning(), declaration_access);
}

analysis_result semantic_analysis::using_declarator(UsingDeclarator n,
                                                    bool if_exists)
{
    if (n.size() == 3 && n[0].is(standard_keyword::typename_) &&
        n[1].is(syntax_kind::nested_name_specifier) && n[2].is_identifier())
        return using_typename(n);
    if (n.size() != 2 || !n.nested_name_specifier())
        return not_supported_because(
            n, "namespace.udecl",
            "a using-declarator with no nested-name-specifier");
    token_index t = 0;
    if (n.identifier()) {
        t = n.identifier().token();
    } else if (auto function = n[1].operator_function_id()) {
        /* [over.match.oper]/3.4: a function a using-declaration brings in
           is a candidate for the reversed or rewritten form of a
           comparison, unless one declared with it ("corresponds") says
           otherwise: not analysed */
        if (function.size() >= 2 && function.symbol().is_token() &&
            tokens[function.symbol().token()].kind ==
                token_kind::operator_or_punctuator)
            switch (preprocessing_op_or_punc_primary(
                tokens[function.symbol().token()].op_or_punc)) {
            case preprocessing_op_or_punc::EQUAL_EQUAL:
            case preprocessing_op_or_punc::EXCLAMATION_EQUAL:
            case preprocessing_op_or_punc::LESS:
            case preprocessing_op_or_punc::GREATER:
            case preprocessing_op_or_punc::LESS_EQUAL:
            case preprocessing_op_or_punc::GREATER_EQUAL:
            case preprocessing_op_or_punc::SPACESHIP:
                return not_supported_because(
                    n, "over.match.oper",
                    "a using-declaration of a comparison operator");
            default:
                break;
            }
        /* [over.oper]/1: operator() names the operator function: the
           keyword's token takes the operator's name */
        declarator_meaning function_name;
        VTRY(r, operator_function_id(function, function_name));
        t = function_name.id;
    } else if (auto literal = n[1].literal_operator_id()) {
        declarator_meaning function_name;
        VTRY(r, literal_operator_id(literal, function_name));
        t = function_name.id;
    } else {
        /* a destructor's name, a conversion function's */
        return not_supported_because(
            n, "namespace.udecl",
            "a using-declaration of a destructor or conversion function");
    }
    nominated_scope q;
    VTRY(r, nested_name_specifier(n.nested_name_specifier(), q));
    uint32_t name = tokens.identifier(t);
    /* [class.qual]/2: the name of the class in its own scope names its
       constructors: [namespace.udecl]/3 inheriting constructors */
    if (defining_class && !is_namespace(q.entity) &&
        entity_table[q.entity].kind == entity_kind::class_type) {
        if (name == entity_table[q.entity].name)
            return inheriting_using(n, t, q.entity);
        entity_index alias = entity_table.find(defining_class, name);
        if (alias && entity_table[alias].kind == entity_kind::typedef_name &&
            entity_table[alias].type.unqualified() ==
                entity_table[q.entity].type.unqualified())
            return inheriting_using(n, t, q.entity);
    }
    lookup_result found = lookup_qualified(q.entity, name, lookup_filter::any);
    /* [namespace.udecl]/4: "If a constructor or assignment operator brought
       from a base class into a derived class has the signature of a
       copy/move constructor or assignment operator for the derived class
       ..., the using-declaration does not by itself suppress the implicit
       declaration of the derived class member; the member from the base
       class is hidden or overridden by the implicitly-declared copy/move
       ... assignment operator of the derived class".  A defined class
       whose only operator= are its implicit ones (which have no entity
       when trivial, class/implicit-assignment.cpp) brings in nothing the
       derived class does not hide: libc++'s <variant>, using
       __base_type::operator= */
    if (defining_class && !found.entity && !found.unknown && !found.ambiguous &&
        name == operator_name(preprocessing_op_or_punc::EQUAL) &&
        !is_namespace(q.entity) &&
        entity_table[q.entity].kind == entity_kind::class_type &&
        entity_table[q.entity].defined && entity_table[q.entity].size)
        return analysis_result::well_formed;
    /* [namespace.udecl]/1: "If the using-declarator names ... a set of
       declarations, the using-declarator introduces the declarator's name
       as a synonym for that set" of overloaded functions and function
       templates: their name is held by an entity of its own
       (held_by_template), which calls resolve the overloads of.  It is
       bound here, or its templates and functions join the set the name
       already denotes here (/11: declarations of the set added later are
       not, which copies of its records give) */
    entity_index held = found.unknown_entity;
    /* [namespace.udecl]/1: a class, alias or variable template's name (held
       by its entity as a function template's is) is a synonym for it
       here: the same entity bound in this scope */
    if (found.unknown && !found.ambiguous && held && !defining_class &&
        is_namespace(q.entity) && !function_template_of(held) &&
        (class_template_of(held) || alias_template_of(held) ||
         variable_template_of(held))) {
        entity_index prior = entity_table.find(current_scope, name);
        if (prior && prior != held)
            return not_supported_because(
                n, "namespace.udecl",
                "a using of a template name already bound otherwise");
        if (!prior)
            entity_table.bind(current_scope, name, held, true);
        used(t, held);
        return analysis_result::well_formed;
    }
    bool class_base_template_using = false;
    if (found.unknown && !found.ambiguous && held && defining_class &&
        !is_namespace(q.entity) &&
        entity_table[q.entity].kind == entity_kind::class_type &&
        member_function_template_of(held)) {
        bool amb = false;
        if (base_step(defining_class, q.entity, amb))
            class_base_template_using = true;
    }
    if (found.unknown && !found.ambiguous && held &&
        ((!defining_class && is_namespace(q.entity) &&
          function_template_of(held)) ||
         class_base_template_using)) {
        entity_index scope = current_scope;
        if (defining_class)
            scope = defining_class;
        entity_index prior = entity_table.find(scope, name);
        if (!prior) {
            entity_table.bind(scope, name, held, true);
        } else if (prior != held) {
            bool poisoned = false;
            if (entity_table[prior].kind == entity_kind::function) {
                /* (as function_template_definition holds the name of
                   functions declared before a template) */
                owned_array<entity_index> functions;
                overload_set(prior, functions);
                prior = entity_table.make(entity_kind::none, name, scope);
                entity_table[prior].held_by_template = true;
                entity_table.bind(scope, name, prior, true);
                for (entity_index f : functions)
                    template_name_functions.append({prior, f});
            } else if (!entity_table[prior].held_by_template) {
                return not_supported_because(
                    n, "over",
                    "a template name using over a non-function declaration");
            } else if (!function_template_of(prior) &&
                       !member_function_template_of(prior)) {
                /* a name whose function templates are all unusable (it
                   also names something calls of them cannot resolve)
                   stays so: what joins it is unusable too, and a call of
                   it is refused */
                for (const function_template &t3 : function_templates)
                    if (t3.name == prior && !t3.is_class && !t3.is_alias &&
                        !t3.is_variable)
                        poisoned = true;
                if (!poisoned)
                    return not_supported_because(n, "over",
                                                 "a template name using over "
                                                 "unusable function templates");
            }
            /* [namespace.udecl]/1, /9: two paths can introduce the same
               declaration. Merge by entity identity, keeping distinct
               functions even when their parameter types match. */
            for (size_t i = 0, k = function_templates.size(); i < k; i++) {
                const function_template &imported = function_templates[i];
                if (imported.name != held || imported.unusable)
                    continue;
                bool present = false;
                for (const function_template &t2 : function_templates)
                    if (t2.name == prior && t2.entity == imported.entity) {
                        present = true;
                        break;
                    }
                if (present)
                    continue;
                function_template copy = imported;
                copy.name = prior;
                copy.unusable = poisoned;
                function_templates.append(copy);
            }
            for (size_t i = 0, k = template_name_functions.size(); i < k; i++) {
                const template_name_function imported =
                    template_name_functions[i];
                if (imported.name != held)
                    continue;
                bool present = false;
                for (const template_name_function &f : template_name_functions)
                    if (f.name == prior && f.function == imported.function) {
                        present = true;
                        break;
                    }
                if (!present)
                    template_name_functions.append({prior, imported.function});
            }
        }
        used(t, held);
        return analysis_result::well_formed;
    }
    if (found.unknown)
        return not_supported_because(
            n, "over", "a using-declaration of a name with unknown lookup");
    /* clang's using_if_exists: "the using-declaration is ignored if the
       name is not found" (a name an unsupported declaration declares
       is not found either, so this trusts the library's headers) */
    if (if_exists && !found.ambiguous && !found.entity)
        return analysis_result::well_formed;
    /* the declarations found in more than one namespace, or none (which
       may be ones analysis has no rules for) */
    if (found.ambiguous || !found.entity)
        return not_supported_because(
            n, "namespace.udecl",
            "a using-declaration of an ambiguous or unfound name");
    entity_index e = found.entity;
    entity_kind kind = entity_table[e].kind;
    bool names_class_member =
        !is_namespace(q.entity) &&
        entity_table[q.entity].kind == entity_kind::class_type;
    if (!is_namespace(q.entity) && !names_class_member)
        /* an enumerator named through its enumeration (C++20) */
        return not_supported_because(
            n, "namespace.udecl",
            "a using-declaration qualified by neither a namespace nor a class");
    if (defining_class) {
        entity_index c = defining_class;
        /* [namespace.udecl]/3: "In a using-declaration used as a
           member-declaration, each using-declarator shall either name an
           enumerator or have a nested-name-specifier naming a base class
           of the class being defined." */
        /* (an enumerator of a namespace's enumeration: C++20) */
        if (is_namespace(q.entity) &&
            (kind == entity_kind::enumerator || kind == entity_kind::none))
            return not_supported_because(
                n, "namespace.udecl",
                "a namespace enumerator or held name in a class using");
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
            return not_supported_because(
                n, "namespace.udecl",
                "a using of a member of a class not found as a base");
        /* [namespace.udecl]/19: "A synonym created by a using-declaration
           has the usual accessibility for a member-declaration."  Access
           is the entity's here, so only a declaration that leaves it as
           it is has rules; a name the derived class declares too would be
           an overload or hide the base's */
        if (kind == entity_kind::none ||
            entity_table[e].access != declaration_access)
            return not_supported_because(
                n, "namespace.udecl",
                "a using of a held name or with a different access");
        /* (the access of every function of the set the head begins) */
        for (entity_index o = e; o; o = entity_table[o].next_overload)
            if (entity_table[o].access != declaration_access)
                return not_supported_because(
                    n, "namespace.udecl",
                    "a using of overloads with differing access");
        entity_index own = entity_table.find(c, name);
        if (own) {
            /* [namespace.udecl]/15 (C++20 /14): the base's member
               functions join the class's own of the name, which hide the
               ones with their signature (overload_set): they follow the
               own in the chain.  A member function template, a name of
               two using-declarations, or any other kind has no rules */
            if (kind != entity_kind::function ||
                entity_table[e].special != special_member::none ||
                entity_table[own].kind != entity_kind::function ||
                entity_table[own].special != special_member::none)
                return not_supported_because(
                    n, "namespace.udecl",
                    "a using of a name the class declares that is no plain "
                    "function");
            if (entity_table[own].enclosing != c)
                /* the name is a base's already (another using-declaration) */
                return using_join_chains(n, t, c, name, own, e);
            entity_index last = own;
            while (entity_table[last].next_overload &&
                   entity_table[entity_table[last].next_overload].enclosing ==
                       c)
                last = entity_table[last].next_overload;
            if (entity_table[last].next_overload == e) {
                /* [namespace.udecl]/9: again */
                used(t, e);
                return analysis_result::well_formed;
            }
            if (entity_table[last].next_overload)
                return using_join_chains(n, t, c, name, own, e);
            entity_table[last].next_overload = e;
            if (entity_table[e].member_templates)
                introduce_member_templates(c, own,
                                           entity_table[e].member_templates);
            used(t, e);
            return analysis_result::well_formed;
        }
        entity_table.bind(c, name, e, true);
        used(t, e);
        return analysis_result::well_formed;
    }
    if (names_class_member) {
        /* [namespace.udecl]/7: "A using-declaration that names a class
           member other than an enumerator shall be a member-declaration." */
        if (kind == entity_kind::enumerator || kind == entity_kind::none)
            return not_supported_because(
                n, "namespace.udecl",
                "a using of an enumerator or held member outside a class");
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
    if (prior_kind == entity_kind::none) {
        if (kind == entity_kind::function &&
            entity_table[prior].held_by_template && is_namespace(scope) &&
            function_template_of(prior)) {
            owned_array<entity_index> functions;
            overload_set(e, functions);
            for (entity_index f : functions) {
                bool present = false;
                for (const template_name_function &x : template_name_functions)
                    if (x.name == prior && x.function == f) {
                        present = true;
                        break;
                    }
                if (!present)
                    template_name_functions.append({prior, f});
            }
            used(t, e);
            return analysis_result::well_formed;
        }
        return not_supported_because(
            n, "basic.scope.declarative",
            "a using over a name held by an unanalysed declaration");
    }
    if (prior == e) {
        /* [namespace.udecl]/9: "A using-declaration is a declaration and can
           therefore be used repeatedly where (and only where) multiple
           declarations are allowed." */
        /* (in a block too: clang, as CWG issue 36 was resolved) */
        if (is_namespace(scope) || entity_table.bound_by_using(scope, name)) {
            used(t, e);
            return analysis_result::well_formed;
        }
        return not_supported_because(n, "namespace.udecl",
                                     "a repeated using-declaration in a class");
    }
    /* [namespace.udecl]/12: "Since a using-declaration is a declaration, the
       restrictions on declarations of the same name in the same declarative
       region also apply to using-declarations." Two functions are overloads; a
       class or enumeration and a variable or function hide one another; a
       typedef-name may name the same type */
    if (kind == entity_kind::function && prior_kind == entity_kind::function)
        return not_supported_because(
            n, "over",
            "a using of a function beside a function of the same name");
    bool plain =
        (kind == entity_kind::variable || kind == entity_kind::function) &&
        (prior_kind == entity_kind::variable ||
         prior_kind == entity_kind::function);
    if (!plain || entity_table[e].hidden_type ||
        entity_table[prior].hidden_type)
        return not_supported_because(
            n, "namespace.udecl",
            "a using that conflicts with a declaration of the name");
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
