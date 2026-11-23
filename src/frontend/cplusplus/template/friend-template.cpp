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
 * Members of dependent types ([temp.friend]/4) and constrained friends
 * ([temp.friend]/9, a requires-clause) are not analysed.
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
            p.keyword_of(tokens[x.token()]) != standard_keyword::none &&
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

/*
 * template<...> friend class-key nested-name-specifier identifier ;  A class
 * template of another scope, a member template of a class or a namespace
 * (template<class> friend struct C::D;): [dcl.type.elab]/1, [dcl.meaning]:
 * a qualified name refers to a template declared before in that scope; all
 * its specializations are friends (/3).  A nested-name-specifier with a
 * simple-template-id is /4's "member of a dependent type", whose friend is
 * the member of each specialization S the template parameters can be
 * deduced from: not analysed.
 */
analysis_result semantic_analysis::qualified_friend_class_template(
    entity_index c, TemplateDeclaration n, SyntaxElement elaborated)
{
    if (!elaborated[1].is(syntax_kind::nested_name_specifier))
        return not_supported(n, "temp.friend");
    if (elaborated[1].size() == 2 &&
        elaborated[1][0].is(syntax_kind::simple_template_id))
        return dependent_member_friend_class(c, n, elaborated[1][0],
                                             elaborated[2].token());
    for (SyntaxElement x : elaborated[1].elements())
        if (!x.is_token() && !x.is_identifier())
            /* A<T>::, decltype(...)::: /4 */
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
    if (!prior || !class_template_of(prior) ||
        entity_table.bound_by_using(scope, name))
        return not_supported(n, "temp.friend");
    befriend_by_name(c, scope, name);
    return analysis_result::well_formed;
}

/*
 * [temp.friend]/4: "A template friend declaration may declare a member of a
 * dependent type to be a friend.  The friend declaration shall ... specify a
 * type with an elaborated-type-specifier ... with a nested-name-specifier
 * ending with a simple-template-id, C, whose template-name names a class
 * template.  The template parameters of the template friend declaration
 * shall be deducible from C.  In this case, a member of a specialization S
 * of the class template is a friend of the class granting friendship if
 * deduction of the template parameters of C from S succeeds, and
 * substituting the deduced template arguments into the friend declaration
 * produces a declaration that would be a valid redeclaration of the member
 * of the specialization."
 *
 * The deduction is analysed for one form of C: its arguments are the
 * friend's type-parameters, each once, in order, and all of the class
 * template's parameters (A<T>, A<T, U>).  Deduction from any specialization
 * of A then succeeds, with S's arguments as the deduced ones (explicit
 * specializations included: "grants friendship to A<int>::B even though it
 * is not a specialization of A<T>::B").  deducible_member_template checks
 * the form and gives A's template entity.
 */
analysis_result semantic_analysis::deducible_member_template(
    TemplateDeclaration n, SyntaxElement id, entity_index &template_entity)
{
    owned_array<SyntaxRef> parameters;
    if (!template_head_list(n.template_head(), parameters) ||
        parameters.empty())
        return not_supported(n, "temp.friend");
    /* the type-parameters' names: class T or typename T, no default, no
       pack */
    owned_array<uint32_t> names;
    for (SyntaxRef parameter : parameters) {
        if (!parameter.is(syntax_kind::type_parameter) ||
            parameter.size() != 2 || !parameter[1].is_identifier())
            return not_supported(n, "temp.friend");
        names.append(tokens.identifier(parameter[1].token()));
    }
    /* C's arguments: a type-id each, the name of one parameter */
    if (!id.is(syntax_kind::simple_template_id) || id.size() < 4 ||
        !id[0].is_identifier())
        return not_supported(n, "temp.friend");
    owned_array<SyntaxElement> arguments;
    for (uint32_t i = 2; i + 1 < id.size(); i++) {
        SyntaxElement x = id[i];
        if (x.is(syntax_kind::template_argument_list)) {
            for (SyntaxElement y : x.elements())
                if (!y.is(preprocessing_op_or_punc::COMMA))
                    arguments.append(y);
        } else if (!x.is(preprocessing_op_or_punc::COMMA)) {
            arguments.append(x);
        }
    }
    if (arguments.size() != names.size())
        return not_supported(n, "temp.friend");
    for (uint32_t i = 0; i < arguments.size(); i++) {
        SyntaxElement a = arguments[i];
        if (!a.is(syntax_kind::type_id) || a.size() != 1 ||
            !a[0].is(syntax_kind::type_specifier_seq) || a[0].size() != 1 ||
            !a[0][0].is_identifier() ||
            tokens.identifier(a[0][0].token()) != names[i])
            return not_supported(n, "temp.friend");
    }
    /* the class template C's template-name names */
    lookup_result found;
    entity_index named = 0;
    if (!template_id_name(id, found, named) || found.ambiguous || !named)
        return not_supported(n, "temp.friend");
    uint32_t k = class_template_of(named);
    if (!k || function_templates[k - 1].parameter_count != names.size())
        return not_supported(n, "temp.friend");
    template_entity = function_templates[k - 1].entity;
    return analysis_result::well_formed;
}

/* template<class T> friend class-key A<T>::B;  C is id, the member's name
   at token name: the class B of every specialization of A (friend_names) */
analysis_result semantic_analysis::dependent_member_friend_class(
    entity_index c, TemplateDeclaration n, SyntaxElement id, token_index name)
{
    entity_index template_entity = 0;
    VTRY(r, deducible_member_template(n, id, template_entity));
    friend_record f;
    f.granting = c;
    f.member_of_template = template_entity;
    f.name = tokens.identifier(name);
    friends.append(f);
    return analysis_result::well_formed;
}

/*
 * [temp.friend]/4 for a function: template<class T> friend R A<T>::f(P);
 * (a constructor or destructor too, friend A<T>::A(P);).  inner is the
 * member-declaration, id is C.  The member of a specialization S is a friend
 * if the friend declaration with S's arguments for the template parameters
 * "would be a valid redeclaration of the member": the declaration is
 * analysed again per specialization (member_function_friends_of, when S is
 * complete), naming S's member by name, parameter-type-list, cv-qualifiers
 * and return type.
 */
analysis_result semantic_analysis::dependent_member_friend_function(
    entity_index c, TemplateDeclaration n, SyntaxRef inner, SyntaxElement id)
{
    entity_index template_entity = 0;
    VTRY(r, deducible_member_template(n, id, template_entity));
    member_function_friends.append({c, template_entity, n, inner});
    /* the specializations complete already */
    for (uint32_t i = 0; i < instantiations.size(); i++) {
        const instantiation_record &x = instantiations[i];
        if (x.template_entity == template_entity && x.specialization &&
            entity_table[x.specialization].kind == entity_kind::class_type &&
            entity_table[x.specialization].defined)
            befriend_member_function(member_function_friends.back(),
                                     x.specialization);
    }
    return analysis_result::well_formed;
}

/* class s, a specialization of a class template, is complete: its members
   that /4 friend declarations name become friends */
void semantic_analysis::member_function_friends_of(entity_index s)
{
    uint32_t i = entity_table[s].instantiation;
    if (!i || member_function_friends.empty())
        return;
    entity_index t = instantiations[i - 1].template_entity;
    for (size_t k = 0; k < member_function_friends.size(); k++)
        if (member_function_friends[k].template_entity == t)
            befriend_member_function(member_function_friends[k], s);
}

/*
 * The friend declaration f with the arguments of specialization s for its
 * template parameters: analysed in a scope where each parameter's name is
 * a typedef-name of its argument, as an instantiation binds them
 * (record_instantiation).  A declaration that does not analyse (T::type
 * with T = int) or names no member of s is no redeclaration, and makes
 * nothing a friend: its diagnostics are taken back.
 */
void semantic_analysis::befriend_member_function(
    const member_function_friend &f, entity_index s)
{
    const instantiation_record &x =
        instantiations[entity_table[s].instantiation - 1];
    owned_array<SyntaxRef> parameters;
    if (!template_head_list(f.declaration.template_head(), parameters) ||
        parameters.size() != x.argument_count)
        return;
    entity_index scope = entity_table.make(entity_kind::block_scope, 0,
                                           enclosing_namespace(f.granting));
    for (uint32_t i = 0; i < x.argument_count; i++) {
        const template_argument &a = template_arguments[x.first_argument + i];
        if (a.kind != template_argument::kind_t::type)
            return;
        uint32_t name = tokens.identifier(parameters[i][1].token());
        entity_index e =
            entity_table.make(entity_kind::typedef_name, name, scope);
        entity_table[e].type = a.type;
        entity_table[e].defined = true;
        entity_table.bind(scope, name, e);
    }
    size_t issued = diagnostics.messages.size();
    size_t refused = unsupported.size();
    entity_index saved_scope = current_scope, saved_class = defining_class;
    SyntaxRef saved_declaration = current_declaration;
    uint32_t saved_cv = function_cv;
    bool saved_friend = friend_allowed;
    current_scope = scope;
    defining_class = 0;
    friend_allowed = true;
    /* the parameters' scope stands for the namespace around the granting
       class, as define_template_member's does for a class template's */
    scoped_value<entity_index> member_scope(template_member_scope, scope);
    SyntaxRef inner = f.inner;
    uint32_t lead = leading_gnu_attributes(inner);
    specified sp;
    analysis_result r = decl_specifier_seq(inner[lead], sp, false);
    declarator_meaning m;
    qualified_type type = sp.type;
    bool no_type = !sp.has_type;
    if (no_type)
        type = qualified_type::make(
            type_table.fundamental(fundamental_type::void_), cv_none);
    if (r == analysis_result::well_formed) {
        function_cv = 0;
        function_parameters.clear();
        friend_declarator = true;
        r = declarator(inner, inner[lead + 1], type, m);
        friend_declarator = false;
    }
    uint32_t cv = function_cv;
    current_scope = saved_scope;
    defining_class = saved_class;
    current_declaration = saved_declaration;
    function_cv = saved_cv;
    friend_allowed = saved_friend;
    diagnostics.discard_since(issued);
    unsupported.resize(refused);
    if (r != analysis_result::well_formed || m.qualifier != s || !m.has_id ||
        type_table[type.unqualified()].kind != type_kind::function)
        return;
    /* the member it would redeclare: same name, parameter-type-list,
       cv-qualifiers and (but for a constructor or destructor) return type
       ([over.load]/2, [class.mem]/5) */
    entity_index member = 0;
    if (m.destructor) {
        member = entity_table[s].destructor;
    } else if (no_type) {
        for (entity_index o = entity_table[s].first_constructor; o;
             o = entity_table[o].next_constructor)
            if (same_parameters(type_table, entity_table[o].type.unqualified(),
                                type.unqualified()))
                member = o;
    } else {
        for (entity_index o = entity_table.find(s, tokens.identifier(m.id));
             o && entity_table[o].kind == entity_kind::function;
             o = entity_table[o].next_overload)
            if (same_parameters(type_table, entity_table[o].type.unqualified(),
                                type.unqualified()) &&
                entity_table[o].member_cv == cv &&
                type_table[entity_table[o].type.unqualified()].operand ==
                    type_table[type.unqualified()].operand)
                member = o;
    }
    if (member)
        befriend(f.granting, member);
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
    if (!inner.is(syntax_kind::member_declaration,
                  syntax_kind::function_definition) ||
        inner.size() != lead + 3)
        return not_supported(n, "temp.friend");
    /* [temp.friend]/1: a member function template of another class befriended:
       template <class T> friend R G::f(P); */
    if (inner.is(syntax_kind::member_declaration) &&
        !inner[lead + 1].is_token()) {
        DeclaratorParts d(inner[lead + 1]);
        /* /4: a member function of a dependent type, A<T>::f */
        if (d.noptr && d.qualifier() && d.qualifier().size() == 2 &&
            d.qualifier()[0].is(syntax_kind::simple_template_id))
            return dependent_member_friend_function(c, n, inner,
                                                    d.qualifier()[0]);
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
    analysis_result r = function_template_definition(n, inner);
    current_scope = saved_scope;
    defining_class = saved_class;
    friend_allowed = saved_friend;
    if (r != analysis_result::well_formed)
        return r;
    if (inner.is(syntax_kind::function_definition))
        function_templates[last_function_template - 1].friend_class = c;
    befriend_template(c, function_templates[last_function_template - 1].entity);
    return analysis_result::well_formed;
}

} // namespace cplusplus
