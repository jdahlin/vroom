/*
 * friend-template.cpp: [temp.friend] friend templates: a function template or
 * class template declared a friend by a template-declaration in a class
 * (the non-template friends, and the specializations a non-template friend
 * declaration names, are class/friend.cpp's).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [temp.friend]/1: "A friend of a class or class template can be a function
 * template or class template, a specialization of a function template or
 * class template, or a non-template function or class": template<...>
 * friend R f(P); in class c.  The template is declared (or redeclared) in
 * the innermost enclosing namespace of c ([namespace.memdef]/3) as by a
 * template-declaration there, and all its specializations are friends.  (The
 * name is then visible to ordinary lookup there, which [namespace.memdef]/3
 * hides until a declaration in the namespace. A definition in the class
 * ([temp.friend]/2) is instantiated in that class's lexical scope.
 * Members of dependent types ([temp.friend]/4) are dependent_member_friend's;
 * a constraint that names an enclosing template's parameter makes the
 * template one of its own per specialization ([temp.friend]/9).
 */
/*
 * template<...> friend class-key identifier ;  [temp.friend]/1: "A friend
 * of a class or class template can be a ... class template"; /2: "a friend
 * class template may not be defined in a class or class template" (as
 * [class.friend]/2, decl-specifier.cpp); /3: "all specializations of that
 * template, whether they are implicitly instantiated, partially specialized
 * or explicitly specialized, are friends".  As a friend class
 * ([namespace.memdef]/3), the prior declaration is looked for in the
 * innermost enclosing namespace only, and with none the template is the
 * one of that name declared there later.  seq is the decl-specifier-seq.
 */
analysis_result semantic_analysis::friend_class_template(entity_index c,
                                                         TemplateDeclaration n,
                                                         SyntaxElement seq)
{
    SyntaxElement elaborated = {};
    bool other_type = false;
    for (SyntaxElement x : seq.elements()) {
        if (x.is(syntax_kind::gnu_attribute_specifier) ||
            attributes_without_effect(x) || x.is(standard_keyword::friend_))
            continue;
        if (x.is(syntax_kind::elaborated_type_specifier) && !elaborated) {
            elaborated = x;
            continue;
        }
        if (is_keyword(x, standard_keyword::const_) ||
            is_keyword(x, standard_keyword::volatile_))
            /* [class.friend]/3: the forms have no cv-qualifier */
            return ill_formed_at_token(x.token(),
                                       "a friend declaration that does not "
                                       "declare a function has no "
                                       "cv-qualifier",
                                       "class.friend");
        /* a type-specifier that is no elaborated-type-specifier: a
           simple-type-specifier or typename-specifier (a keyword other than
           typename has no rules here) */
        if (x.is_token() &&
            syntax_parser.keyword_of(tokens[x.token()]) !=
                standard_keyword::none &&
            !is_keyword(x, standard_keyword::typename_))
            return not_supported(n, "temp.friend");
        other_type = true;
    }
    if (elaborated && other_type)
        return not_supported(n, "temp.friend");
    /* [temp.pre]/2: "The declaration in a template-declaration (if any)
       shall declare or define a function, a class, or a variable": template
       <class U> friend S<U>; declares none */
    if (!elaborated)
        return ill_formed(seq,
                          "a friend class template is declared by an "
                          "elaborated-type-specifier",
                          "temp.pre");
    if (!is_class_key(leading_keyword(elaborated)))
        return not_supported(n, "temp.friend");
    if (elaborated.size() == 2 &&
        elaborated[1].is(syntax_kind::simple_template_id))
        /* /7: "Friend declarations shall not declare partial
           specializations." */
        return ill_formed(elaborated,
                          "friend declarations shall not declare partial "
                          "specializations",
                          "temp.friend");
    if (elaborated.size() == 3 && elaborated[2].is_identifier())
        return qualified_friend_class_template(c, n, elaborated);
    if (elaborated.size() != 2 || !elaborated[1].is_identifier())
        return not_supported(n, "temp.friend");
    uint32_t name = tokens.identifier(elaborated[1].token());
    entity_index space = enclosing_namespace(c);
    if (entity_table[space].unknown_names ||
        tokens.files.behavior.microsoft_extensions)
        return not_supported(n, "temp.friend");
    if (entity_index prior = entity_table.find(space, name)) {
        /* a class template of the namespace: its name is held
           (class_template_of); anything else bound there is not analysed */
        if (!class_template_of(prior) ||
            entity_table.bound_by_using(space, name))
            return not_supported(n, "temp.friend");
    }
    /* every specialization of the class template of that name: a
       specialization is a class of the template's scope with its name
       (friend_names) */
    befriend_by_name(c, space, name);
    return analysis_result::well_formed;
}

/* the class-name or simple-template-id a nested-name-specifier ends with
   (the last component before its last ::), and whether an earlier
   component is a simple-template-id */
static SyntaxElement last_component(SyntaxElement nested, bool &earlier_id)
{
    SyntaxElement last = {};
    earlier_id = false;
    for (SyntaxElement x : nested.elements()) {
        if (x.is(preprocessing_op_or_punc::COLON_COLON) ||
            x.is(standard_keyword::template_))
            continue;
        if (last && last.is(syntax_kind::simple_template_id))
            earlier_id = true;
        last = x;
    }
    return last;
}

/*
 * template<...> friend class-key nested-name-specifier identifier ;
 *
 * A nested-name-specifier that ends with a simple-template-id is /4's
 * member of a dependent type (dependent_member_friend).  "The friend
 * declaration shall ... specify a type with an elaborated-type-specifier,
 * ... with a nested-name-specifier ending with a simple-template-id": one
 * that names a dependent type further in (A<T>::B::C, CWG1918) is
 * ill-formed.
 *
 * Otherwise a class template of another scope, a member template of a
 * class or a namespace (template<class> friend struct C::D;):
 * [dcl.type.elab]/1, [dcl.meaning]: a qualified name refers to a template
 * declared before in that scope; all its specializations are friends (/3).
 */
analysis_result semantic_analysis::qualified_friend_class_template(
    entity_index c, TemplateDeclaration n, SyntaxElement elaborated)
{
    if (!elaborated[1].is(syntax_kind::nested_name_specifier))
        return not_supported(n, "temp.friend");
    bool earlier_id = false;
    SyntaxElement last = last_component(elaborated[1], earlier_id);
    if (last.is(syntax_kind::simple_template_id))
        return dependent_member_friend(c, n, elaborated, last);
    if (earlier_id)
        return ill_formed(elaborated[1],
                          "the nested-name-specifier of a friend member of a "
                          "dependent type ends with a simple-template-id",
                          "temp.friend");
    for (SyntaxElement x : elaborated[1].elements())
        if (!x.is_token() && !x.is_identifier())
            /* decltype(...):: */
            return not_supported(n, "temp.friend");
    nominated_scope ns;
    VTRY(r, nested_name_specifier(elaborated[1].index(), ns));
    entity_index scope = ns.entity;
    if (!scope || ns.dependent ||
        (!is_namespace(scope) &&
         entity_table[scope].kind != entity_kind::class_type))
        return not_supported(n, "temp.friend");
    uint32_t name = tokens.identifier(elaborated[2].token());
    entity_index prior = entity_table.find(scope, name);
    if (!prior && is_namespace(scope)) {
        /* [namespace.def]/6: a member of an inline namespace of the
           namespace is found by a qualified name of it
           (::nlohmann::json_pointer, declared in an inline namespace) */
        lookup_result q = lookup_qualified(scope, name, lookup_filter::any);
        entity_index found = q.entity;
        if (!found)
            found = q.unknown_entity;
        if (found && class_template_of(found)) {
            befriend_template(
                c, function_templates[class_template_of(found) - 1].entity);
            return analysis_result::well_formed;
        }
    }
    if (!prior || !class_template_of(prior) ||
        entity_table.bound_by_using(scope, name))
        return not_supported(n, "temp.friend");
    befriend_by_name(c, scope, name);
    return analysis_result::well_formed;
}

/*
 * [temp.friend]/4: "A template friend declaration may declare a member of a
 * dependent type to be a friend.  The friend declaration shall declare a
 * function or specify a type with an elaborated-type-specifier, in either
 * case with a nested-name-specifier ending with a simple-template-id, C,
 * whose template-name names a class template.  The template parameters of
 * the template friend declaration shall be deducible from C.  In this case,
 * a member of a specialization S of the class template is a friend of the
 * class granting friendship if deduction of the template parameters of C
 * from S succeeds, and substituting the deduced template arguments into the
 * friend declaration produces a declaration that would be a valid
 * redeclaration of the member of the specialization."
 *
 * declaration is the elaborated-type-specifier, or the member-declaration
 * of a function; id is C.  C's arguments are analysed once, in the scope of
 * the template-parameters, as a partial specialization's are
 * (class_specialization_definition); the rest is done per specialization S
 * (member_friends_of): S's arguments deduce the parameters from C's
 * ([temp.deduct.type], deduce_argument, as [temp.class.spec.match] does),
 * the declaration is analysed again with the deduced arguments, and the
 * member of S it would validly redeclare becomes the friend.  That covers
 * explicit and partial specializations of the class template: "grants
 * friendship to A<int>::B even though it is not a specialization of
 * A<T>::B".
 */
analysis_result semantic_analysis::dependent_member_friend(
    entity_index c, TemplateDeclaration n, SyntaxElement declaration,
    SyntaxElement id)
{
    lookup_result found;
    entity_index named = 0;
    if (!template_id_name(id, found, named) || found.ambiguous || !named)
        return not_supported(n, "temp.friend");
    uint32_t ct = class_template_of(named);
    if (!ct)
        return not_supported(n, "temp.friend");
    owned_array<SyntaxRef> parameters;
    entity_index saved_class = defining_class;
    defining_class = 0;
    analysis_result r =
        template_head_parameters(n, parameters, true, true, true, true);
    defining_class = saved_class;
    if (r != analysis_result::well_formed)
        return r;
    /* "The template parameters ... shall be deducible from C": each
       named in an argument of C that is a deduced context
       ([temp.deduct.type]/5.7: not "a template parameter pack expansion
       that is not the last argument"; other non-deduced contexts, T::type,
       are taken as deduced) */
    owned_array<SyntaxElement> written;
    owned_array<bool> expansion;
    for (SyntaxElement x : id.elements())
        if (x.is(syntax_kind::template_argument_list) &&
            !expansion_list(x, written, expansion))
            return not_supported(n, "temp.friend");
    if (written.empty())
        for (uint32_t i = 2; i + 1 < id.size(); i++) {
            written.append(id[i]);
            expansion.append(false);
        }
    for (SyntaxRef parameter : parameters) {
        token_index name_at = 0;
        for (token_index t = parameter.first_token();
             t < tree[parameter].end_token; t++)
            if (tokens[t].kind == token_kind::identifier)
                name_at = t;
        if (!name_at)
            return not_supported(n, "temp.friend");
        bool named_in_c = false;
        for (size_t a = 0; a < written.size(); a++) {
            /* a pack expansion: an ellipsis after the argument, or ending
               its type-id (Ts...) */
            token_index last_token = tree[written[a]].end_token - 1;
            bool pack =
                expansion[a] || (tokens[last_token].kind ==
                                     token_kind::operator_or_punctuator &&
                                 preprocessing_op_or_punc_primary(
                                     tokens[last_token].op_or_punc) ==
                                     preprocessing_op_or_punc::ELLIPSIS);
            if (pack && a + 1 != written.size())
                continue;
            for (token_index t = written[a].first_token();
                 t < tree[written[a]].end_token; t++)
                if (tokens[t].kind == token_kind::identifier &&
                    tokens.identifier(t) == tokens.identifier(name_at))
                    named_in_c = true;
        }
        if (!named_in_c)
            return ill_formed(parameter,
                              "the template parameters of a friend member "
                              "of a dependent type shall be deducible from "
                              "its class",
                              "temp.friend");
    }
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY(sr, template_parameter_scope(n, parameters, enclosing_namespace(c), 0,
                                      index, scope, first));
    owned_array<template_argument> arguments;
    partial_arguments++;
    declaring_specializations++;
    r = template_id_arguments_in(id, ct, scope, arguments);
    partial_arguments--;
    declaring_specializations--;
    if (r != analysis_result::well_formed)
        return r;
    member_friend f;
    f.granting = c;
    f.template_entity = function_templates[ct - 1].entity;
    f.declaration = n;
    f.member = declaration;
    f.first_parameter = first;
    f.parameter_count = static_cast<uint32_t>(parameters.size());
    f.first_pattern = static_cast<uint32_t>(template_arguments.size());
    f.pattern_count = static_cast<uint32_t>(arguments.size());
    for (const template_argument &a : arguments)
        template_arguments.append(a);
    member_friends.append(f);
    /* clang: a friend class of this form "is not supported; turning off
       access control for" the granting class, every specialization of
       it (Sema::ActOnTemplatedFriendTag, FriendDecl's unsupported
       friend) */
    if (declaration.is(syntax_kind::elaborated_type_specifier)) {
        entity_table[c].access_unchecked = true;
        if (entity_table[c].instantiation)
            entity_table[specialized_template(c)].access_unchecked = true;
    }
    /* the specializations complete already */
    uint32_t added = static_cast<uint32_t>(member_friends.size() - 1);
    for (uint32_t i = 0; i < instantiations.size(); i++) {
        entity_index s = instantiations[i].specialization;
        if (instantiations[i].template_entity == f.template_entity && s &&
            entity_table[s].kind == entity_kind::class_type &&
            entity_table[s].defined)
            befriend_member(added, s);
    }
    return analysis_result::well_formed;
}

/* class s, a specialization of a class template, begins (complete false)
   or is complete: the members /4 friend declarations name become friends.
   A member class is named when s begins, so that its own member
   declarations, analysed inside s's, are in a friend (friend_names finds
   it by name in s); a member function when s is complete and its members
   are declared. */
void semantic_analysis::member_friends_of(entity_index s, bool complete)
{
    uint32_t i = entity_table[s].instantiation;
    if (!i || member_friends.empty())
        return;
    entity_index t = instantiations[i - 1].template_entity;
    for (uint32_t k = 0; k < member_friends.size(); k++)
        if (member_friends[k].template_entity == t &&
            member_friends[k].member.is(
                syntax_kind::elaborated_type_specifier) != complete)
            befriend_member(k, s);
}

/*
 * member_friends[k] for specialization s: deduction, substitution, and the
 * member redeclared.  A deduction that fails, a declaration that does not
 * analyse with the deduced arguments (T::type with T = int) or that names
 * no member of s is no redeclaration and makes nothing a friend: what its
 * analysis diagnosed or refused is taken back.
 */
void semantic_analysis::befriend_member(uint32_t k, entity_index s)
{
    member_friend f = member_friends[k];
    uint32_t record = entity_table[s].instantiation - 1;
    uint32_t first_argument = instantiations[record].first_argument;
    uint32_t argument_count = instantiations[record].argument_count;
    if (argument_count != f.pattern_count)
        return;
    /* [temp.deduct.type]: the parameters from C's arguments and S's */
    owned_array<template_argument> deduced;
    owned_array<bool> set;
    for (uint32_t i = 0; i < f.parameter_count; i++) {
        deduced.append(template_argument());
        set.append(false);
    }
    uint32_t unknowns = deduction_unknowns;
    bool match = true;
    matching_partial = true;
    for (uint32_t i = 0; i < argument_count && match; i++) {
        const template_argument x = template_arguments[f.first_pattern + i];
        const template_argument y = template_arguments[first_argument + i];
        if (x.kind == template_argument::kind_t::pack &&
            y.kind == template_argument::kind_t::pack)
            match = deduce_pack_argument(x, y, deduced, set);
        else
            match = deduce_argument(x, y, deduced, set);
    }
    matching_partial = false;
    deduction_unknowns = unknowns;
    if (!match)
        return;
    for (uint32_t i = 0; i < f.parameter_count; i++)
        if (!set[i])
            return;
    /* the declaration with the deduced arguments, in a scope where each
       parameter's name is its argument (record_instantiation), standing
       for the namespace around the granting class */
    /* for a function, a scope in s (whose parameters the declarator then
       analyses there, [basic.lookup.qual]/3, keeping the parameters' names
       as a member template's out-of-class definition does) */
    entity_index outer = enclosing_namespace(f.granting);
    if (!f.member.is(syntax_kind::elaborated_type_specifier))
        outer = s;
    entity_index scope = entity_table.make(entity_kind::block_scope, 0, outer);
    for (uint32_t i = 0; i < f.parameter_count; i++)
        bind_template_argument(
            scope, template_parameter_entities[f.first_parameter + i],
            deduced[i]);
    size_t issued = diagnostics.messages.size();
    size_t refused = unsupported.size();
    entity_index saved_scope = current_scope, saved_class = defining_class;
    SyntaxRef saved_declaration = current_declaration;
    uint32_t saved_cv = function_cv;
    bool saved_friend = friend_allowed;
    current_scope = scope;
    defining_class = 0;
    friend_allowed = true;
    entity_index member = 0;
    bool member_class = false;
    {
        scoped_value<entity_index> member_scope(template_member_scope, scope);
        /* [class.friend]/9: "A name nominated by a friend declaration
           shall be accessible in the scope of the class containing the
           friend declaration": its names are used as the granting class's
           (typename T::type with T = the class itself, a private member) */
        scoped_value<entity_index> access(access_class, f.granting);
        if (f.member.is(syntax_kind::elaborated_type_specifier))
            member_class = redeclares_member_class(f.member, s);
        else
            member = redeclared_member_function(f.member, s);
    }
    current_scope = saved_scope;
    defining_class = saved_class;
    current_declaration = saved_declaration;
    function_cv = saved_cv;
    friend_allowed = saved_friend;
    diagnostics.discard_since(issued);
    unsupported.resize(refused);
    if (member)
        befriend(f.granting, member);
    if (member_class)
        befriend_by_name(f.granting, s, tokens.identifier(f.member[2].token()));
}

/* class-key A<args>::B with the arguments bound: whether it names s, so
   that it would redeclare s's member class B (declared later in s's
   definition, or before) */
bool semantic_analysis::redeclares_member_class(SyntaxElement elaborated,
                                                entity_index s)
{
    if (elaborated.size() != 3 || !elaborated[2].is_identifier())
        return false;
    nominated_scope ns;
    return nested_name_specifier(elaborated[1].index(), ns) ==
               analysis_result::well_formed &&
           ns.entity == s;
}

/* R A<args>::f(P) cv with the arguments bound: the member function of s
   it redeclares, by name, parameter-type-list, cv-qualifiers and (but for
   a constructor or destructor) return type ([over.load]/2, [class.mem]/5);
   a conversion function by its type and cv-qualifiers */
entity_index semantic_analysis::redeclared_member_function(SyntaxElement inner,
                                                           entity_index s)
{
    uint32_t lead = leading_gnu_attributes(inner);
    specified sp;
    if (decl_specifier_seq(inner[lead], sp, false) !=
        analysis_result::well_formed)
        return 0;
    qualified_type type = sp.type;
    bool no_type = !sp.has_type;
    if (no_type)
        type = qualified_type::make(
            type_table.fundamental(fundamental_type::void_), cv_none);
    /* [class.conv.fct]/1: a conversion function's declarator has no type
       before it; its conversion-type-id is the function's return type */
    SyntaxRef conversion = conversion_function_id_of(inner);
    if (conversion && qualified_conversion_type(inner, conversion, type) !=
                          analysis_result::well_formed)
        return 0;
    declarator_meaning m;
    function_cv = 0;
    function_parameters.clear();
    friend_declarator = true;
    analysis_result r = declarator(inner, inner[lead + 1], type, m);
    friend_declarator = false;
    if (r != analysis_result::well_formed || m.qualifier != s || !m.has_id ||
        type_table[type.unqualified()].kind != type_kind::function)
        return 0;
    uint32_t cv = function_cv;
    if (m.destructor)
        return entity_table[s].destructor;
    if (conversion)
        /* the conversion function of that type and cv-qualifiers */
        return overload_of_type(own_conversion_functions(s), type, cv);
    entity_index member = 0;
    if (no_type) {
        for (entity_index o = entity_table[s].first_constructor; o;
             o = entity_table[o].next_constructor)
            if (same_parameters(type_table, entity_table[o].type.unqualified(),
                                type.unqualified()))
                member = o;
        return member;
    }
    for (entity_index o = entity_table.find(s, tokens.identifier(m.id));
         o && entity_table[o].kind == entity_kind::function;
         o = entity_table[o].next_overload)
        if (same_parameters(type_table, entity_table[o].type.unqualified(),
                            type.unqualified()) &&
            entity_table[o].member_cv == cv &&
            type_table[entity_table[o].type.unqualified()].operand ==
                type_table[type.unqualified()].operand)
            member = o;
    return member;
}

/* whether a constraint of friend template declaration n (its template-head's
   requires-clause, or the trailing one of its declarator) names a template
   parameter of a template c is a specialization of, or is in, or a member
   of such a class: [temp.friend]/9's "constraint that depends on a
   template parameter from an enclosing template" (by name: the
   specialization's arguments are bound to those names) */
bool semantic_analysis::constraint_names_enclosing_parameter(
    entity_index c, TemplateDeclaration n) const
{
    owned_array<uint32_t> names;
    for (entity_index k = c; !is_namespace(k); k = entity_table[k].enclosing) {
        uint32_t i = entity_table[k].instantiation;
        if (!i || entity_table[k].kind != entity_kind::class_type)
            continue;
        /* the primary template's record: its parameters' names */
        entity_index te = instantiations[i - 1].template_entity;
        for (const function_template &t : function_templates) {
            if (t.entity != te || !t.is_class || t.primary)
                continue;
            for (uint32_t q = 0; q < t.parameter_count; q++)
                names.append(
                    entity_table[template_parameter_entities[t.first_parameter +
                                                             q]]
                        .name);
        }
    }
    if (names.empty())
        return false;
    /* the names a constraint has: the template-head's (type-constraints,
       its requires-clause) and those after a requires in the declarator
       (a trailing requires-clause, to the end of the declaration: the
       body's names are the function's) */
    auto names_dependent = [&](token_index from, token_index to) {
        for (token_index u = from; u < to; u++) {
            if (tokens[u].kind != token_kind::identifier)
                continue;
            uint32_t id = tokens.identifier(u);
            for (uint32_t name : names)
                if (id == name)
                    return true;
            /* a member of the templated class (T variable; using Alias =
               T;): [temp.dep.expr]/3, [temp.dep.type]/7 make it
               dependent on the enclosing parameters */
            for (entity_index k = c; !is_namespace(k);
                 k = entity_table[k].enclosing)
                if (entity_table[k].kind == entity_kind::class_type &&
                    entity_table.find(k, id))
                    return true;
        }
        return false;
    };
    SyntaxRef head = n.template_head();
    owned_array<SyntaxElement> parts;
    for (SyntaxElement x : head.elements()) {
        if (x.is(syntax_kind::template_parameter_list)) {
            for (SyntaxElement y : x.elements())
                parts.append(y);
        } else {
            parts.append(x);
        }
    }
    for (SyntaxElement x : parts) {
        if (x.is_token())
            continue;
        /* the head's requires-clause, and a type-parameter's
           type-constraint (same_as<T> U: its first part when that is no
           class or typename); not a default template argument */
        SyntaxElement constraint = {};
        if (x.is(syntax_kind::requires_clause))
            constraint = x;
        else if (x.is(syntax_kind::type_parameter) && x.size() &&
                 !x[0].is_token())
            constraint = x[0];
        if (constraint && names_dependent(constraint.first_token(),
                                          tree[constraint].end_token))
            return true;
    }
    for (token_index t = tree[head].end_token; t < tree[n].end_token; t++) {
        if (syntax_parser.keyword_of(tokens[t]) != standard_keyword::requires_)
            continue;
        token_index end = tree[n].end_token;
        /* a definition: up to its body */
        SyntaxRef inner = n[1];
        if (inner.is(syntax_kind::function_definition))
            end = inner.function_definition().function_body().first_token();
        return names_dependent(t + 1, end);
    }
    return false;
}

analysis_result semantic_analysis::friend_function_template(
    entity_index c, TemplateDeclaration n, SyntaxRef inner)
{
    /* [temp.friend]/6: "A friend template shall not be declared in a local
       class." */
    if (local_scope_of(c))
        return ill_formed(n,
                          "a friend template shall not be declared in a "
                          "local class",
                          "temp.friend");
    uint32_t lead = leading_gnu_attributes(inner);
    if (inner.is(syntax_kind::member_declaration) && inner.size() == lead + 2 &&
        inner[lead].is(syntax_kind::decl_specifier_seq) &&
        inner[lead + 1].is(preprocessing_op_or_punc::SEMICOLON))
        return friend_class_template(c, n, inner[lead]);
    /* a definition with a trailing requires-clause between its declarator
       and its body ([dcl.decl]/1; function_template_definition checks it,
       trailing_constraint) */
    /* /9: "A friend function template with a constraint that depends on a
       template parameter from an enclosing template shall be a
       definition." */
    bool enclosing_constraint = constraint_names_enclosing_parameter(c, n);
    if (inner.is(syntax_kind::member_declaration) && enclosing_constraint)
        return ill_formed(n,
                          "a friend function template with a constraint "
                          "that depends on a template parameter from an "
                          "enclosing template shall be a definition",
                          "temp.friend");
    bool trailing = inner.is(syntax_kind::function_definition) &&
                    inner.size() == lead + 4 &&
                    inner[lead + 2].is(syntax_kind::requires_clause);
    if (!inner.is(syntax_kind::member_declaration,
                  syntax_kind::function_definition) ||
        (inner.size() != lead + 3 && !trailing))
        return not_supported(n, "temp.friend");
    /* [temp.friend]/1: a member function template of another class befriended:
       template <class T> friend R G::f(P); */
    if (inner.is(syntax_kind::member_declaration) &&
        !inner[lead + 1].is_token()) {
        DeclaratorParts d(inner[lead + 1]);
        /* /4: a member function of a dependent type, A<T>::f; one whose
           nested-name-specifier names a dependent type further in
           (A<T>::D::g) is ill-formed */
        if (d.noptr && d.qualifier()) {
            bool earlier_id = false;
            SyntaxElement last = last_component(d.qualifier(), earlier_id);
            if (last.is(syntax_kind::simple_template_id))
                return dependent_member_friend(c, n, inner, last);
            if (earlier_id)
                return ill_formed(d.qualifier(),
                                  "the nested-name-specifier of a friend "
                                  "member of a dependent type ends with a "
                                  "simple-template-id",
                                  "temp.friend");
        }
        if (d.noptr && d.qualifier()) {
            nominated_scope ns;
            VTRY(nr, nested_name_specifier(d.qualifier().index(), ns));
            if (is_class_entity(ns.entity)) {
                entity_index target = ns.entity;
                owned_array<SyntaxRef> parameters;
                entity_index saved_class = defining_class;
                defining_class = 0;
                analysis_result hr = template_head_parameters(
                    n, parameters, true, true, true, true);
                defining_class = saved_class;
                if (hr != analysis_result::well_formed)
                    return hr;
                uint32_t number = 0, first = 0;
                entity_index scope = 0;
                VTRY(tr, template_parameter_scope(n, parameters, target, 0,
                                                  number, scope, first));
                specified s;
                declarator_meaning m;
                qualified_type type;
                entity_index saved_scope = current_scope;
                uint32_t saved_cv = function_cv;
                bool saved_friend = friend_allowed;
                scoped_value<entity_index> member_scope(template_member_scope,
                                                        scope);
                defining_class = 0;
                friend_allowed = true;
                analysis_result dr = function_template_declarator(
                    inner, lead, target, scope, s, m, type);
                uint32_t cv = function_cv;
                current_scope = saved_scope;
                defining_class = saved_class;
                function_cv = saved_cv;
                friend_allowed = saved_friend;
                if (dr != analysis_result::well_formed)
                    return dr;
                if (!m.has_id || m.qualifier != target)
                    return not_supported(n, "temp.friend");
                entity_index held =
                    entity_table.find(target, tokens.identifier(m.id));
                if (held)
                    held = member_templates_of(held);
                if (!held)
                    return not_supported(n, "temp.friend");
                owned_array<uint32_t> group;
                function_template_overloads(held, group);
                for (uint32_t g : group) {
                    const function_template f = function_templates[g - 1];
                    const entity old = entity_table[f.entity];
                    if (old.enclosing != target || old.member_cv != cv)
                        continue;
                    if (!equivalent_template(
                            f, n, first,
                            static_cast<uint32_t>(parameters.size()), type))
                        continue;
                    befriend_template(c, f.entity);
                    return analysis_result::well_formed;
                }
                return not_supported(n, "temp.friend");
            }
        }
    }
    entity_index saved_scope = current_scope, saved_class = defining_class;
    bool saved_friend = friend_allowed;
    current_scope = enclosing_namespace(c);
    defining_class = c;
    friend_allowed = true;
    last_function_template = 0;
    /* /9: "Such a constrained friend function template declaration does
       not declare the same function template as a declaration in any
       other scope": each specialization of the enclosing template defines
       a template of its own */
    scoped_value<bool> distinct(distinct_friend_template, enclosing_constraint);
    analysis_result r = function_template_definition(n, inner);
    current_scope = saved_scope;
    defining_class = saved_class;
    friend_allowed = saved_friend;
    if (r != analysis_result::well_formed)
        return r;
    /* (a declaration too: its specializations' declarations are analysed
       with its friend specifier, instantiate) */
    function_templates[last_function_template - 1].friend_class = c;
    if (!inner.is(syntax_kind::function_definition))
        function_templates[last_function_template - 1].friend_declaration =
            true;
    befriend_template(c, function_templates[last_function_template - 1].entity);
    return analysis_result::well_formed;
}

} // namespace cplusplus
