/*
 * friend.cpp: [class.friend] friends (doc/PLAN-cxx-sema.md R10).
 *
 * [class.friend]/1: "A friend of a class is a function or class that is
 * given permission to use the private and protected member names from the
 * class.  A class specifies its friends, if any, by way of friend
 * declarations.  Such declarations give special access rights to the
 * friends, but they do not make the nominated friends members of the
 * befriending class."
 *
 * The paragraphs and where they are:
 *
 *   /1, /2, /10  the relation: befriend, friend_names, grants, in_friend_of;
 *                class-specifier.cpp's accessible() asks it.  Friendship
 *                is a pair (granting class, befriended entity): it is
 *                neither inherited nor transitive (/10) because nothing
 *                follows a base or another friend record from it.
 *   /2           "A class shall not be defined in a friend declaration":
 *                decl-specifier.cpp, where the class-specifier is.
 *   /3           the three forms of a friend declaration that declares no
 *                function: friend_type, friend_elaborated_class.
 *   /4, /5       a friend function, its linkage and the one function of an
 *                overload set it names: friend_function,
 *                friend_member_function (with implicitly_declares for a
 *                special member not declared yet), qualified_friend_function.
 *   /6, /7       a function defined in a friend declaration:
 *                friend_function, friend_function_body, lexical_parent.
 *   /8           no storage-class-specifier: friend_function.
 *   /9           access to the name nominated: the lookups here check it as
 *                any use of a name.
 *   /11          a friend declaration in a local class: local_scope_of,
 *                friend_elaborated_class, local_friend_function.
 *
 * [temp.friend]/9's non-template constrained friend is
 * constrained_friend_function; the rest of [temp.friend] (friend templates,
 * members of dependent types) is template/friend-template.cpp;
 * [class.compare.default] (a defaulted friend operator==) is
 * class/defaulted-comparison.cpp.  A class template's friends are declared
 * per specialization (instantiate_class), each granting only that
 * specialization ([temp.friend]/1).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* ---- the friend relation: [class.friend]/1, /2, /10 ---------------------- */

/* [class.friend]/1: c grants friendship to e.  Every friend declaration
   that names its friend ends here, in befriend_by_name or in
   befriend_template. */
void semantic_analysis::befriend(entity_index c, entity_index e)
{
    friend_record f;
    f.granting = c;
    f.befriended = e;
    friends.append(f);
}

/* [class.friend]/3, [namespace.memdef]/3: a friend class not declared yet:
   the class of this name in scope, when it is declared */
void semantic_analysis::befriend_by_name(entity_index c, entity_index scope,
                                         uint32_t name)
{
    friend_record f;
    f.granting = c;
    f.scope = scope;
    f.name = name;
    friends.append(f);
}

/* [temp.friend]/1, /3: every specialization of template t (a template's
   entity) is a friend of c */
void semantic_analysis::befriend_template(entity_index c, entity_index t)
{
    friend_record f;
    f.granting = c;
    f.template_entity = t;
    friends.append(f);
}

/* whether the friend declaration f names e */
bool semantic_analysis::friend_names(const friend_record &f,
                                     entity_index e) const
{
    if (f.template_entity) {
        /* [temp.friend]/3: "A template friend declaration specifies that
           all specializations of that template ... are friends" */
        if (e == f.template_entity)
            return true;
        uint32_t i = entity_table[e].instantiation;
        return i && instantiations[i - 1].template_entity == f.template_entity;
    }
    if (f.befriended)
        return f.befriended == e;
    const entity &x = entity_table[e];
    return x.kind == entity_kind::class_type && x.enclosing == f.scope &&
           x.name == f.name;
}

/* whether class k declares e its friend.  [class.friend]/10: "Friendship is
   neither inherited nor transitive": only k's own friend declarations
   count, never those of k's bases or of k's friends. */
bool semantic_analysis::grants(entity_index k, entity_index e) const
{
    for (const friend_record &f : friends)
        if (f.granting == k && friend_names(f, e))
            return true;
    return false;
}

/* whether the name used now is in a friend of class k: the function
   analysed, the class whose member-specification is, or a class or
   function around them.  [class.friend]/2: a befriended class's
   base-specifiers and member declarations, nested classes' among them
   ([class.access.nest]/1), may use k's private and protected names;
   [class.local]/1 gives a local class its function's access. */
bool semantic_analysis::in_friend_of(entity_index k) const
{
    entity_index starts[4] = {0, defining_class, 0, access_class};
    if (function_state.active)
        starts[0] = function_state.function;
    if (!is_namespace(current_scope))
        starts[2] = current_scope;
    for (entity_index start : starts)
        for (entity_index here = start; !is_namespace(here);
             here = entity_table[here].enclosing)
            if (grants(k, here))
                return true;
    return false;
}

/* ---- a friend declaration ------------------------------------------------ */

/*
 * [class.local]: the innermost enclosing non-class scope of class c when
 * that is a block scope (c is a local class, or a class nested in one), 0
 * when it is a namespace.  [class.friend]/11 restricts the lookups of a
 * friend declaration in such a class to that scope.
 */
entity_index semantic_analysis::local_scope_of(entity_index c) const
{
    entity_index k = c;
    while (!is_namespace(k) && entity_table[k].kind == entity_kind::class_type)
        k = entity_table[k].enclosing;
    if (is_namespace(k))
        return 0;
    return k;
}

/* a member-declaration whose decl-specifier-seq has friend */
analysis_result
semantic_analysis::friend_declaration(entity_index c, SyntaxRef n,
                                      owned_array<SyntaxRef> &bodies)
{
    return n.member_declaration().visit(Overloaded{
        [&](FunctionDefinition f) { return friend_function(c, f, bodies); },
        [&](MemberSimpleDeclaration d) {
            return friend_member_declaration(c, d, bodies);
        },
        [&](MemberDeclaration) { return not_supported(n, "class.friend"); }});
}

/* a friend member-declaration that is not a function-definition: with no
   declarator it declares a type a friend ([class.friend]/3), with one a
   function */
analysis_result semantic_analysis::friend_member_declaration(
    entity_index c, MemberSimpleDeclaration d, owned_array<SyntaxRef> &bodies)
{
    SyntaxRef n = d;
    uint32_t leading = leading_gnu_attributes(n);
    SyntaxTail tail(n, leading);
    if (tail.size() < 2)
        return not_supported(n, "class.friend");
    if (tail.size() == 2)
        return friend_type(c, n, tail[0]);
    return friend_function(c, n, bodies);
}

/* ---- a friend type: [class.friend]/2, /3 --------------------------------- */

/*
 * [class.friend]/3: "A friend declaration that does not declare a function
 * shall have one of the following forms:
 *
 *     friend elaborated-type-specifier ;
 *     friend simple-type-specifier ;
 *     friend typename-specifier ;
 *
 * ... If the type specifier in a friend declaration designates a (possibly
 * cv-qualified) class type, that class is declared as a friend; otherwise,
 * the friend declaration is ignored."  seq is the decl-specifier-seq, the
 * declaration's only part before the semicolon.  The forms put friend
 * first and allow no cv-qualifier.  [impl.clang] A simple-type-specifier of
 * several keywords (friend unsigned int;) is one type, as clang takes it;
 * before C++11 only the first form was allowed, and the others are accepted
 * there as clang's extension is.
 */
analysis_result semantic_analysis::friend_type(entity_index c, SyntaxRef n,
                                               SyntaxElement seq)
{
    SyntaxElement elaborated = {};
    bool first = true;
    for (SyntaxElement x : seq.elements()) {
        if (x.is(syntax_kind::gnu_attribute_specifier) ||
            attributes_without_effect(x))
            continue;
        if (first && !is_keyword(x, standard_keyword::friend_))
            return ill_formed(n,
                              "a friend declaration that does not declare a "
                              "function begins with friend",
                              "class.friend");
        first = false;
        if (x.is_token()) {
            token_index t = x.token();
            if (is_keyword(x, standard_keyword::const_) ||
                is_keyword(x, standard_keyword::volatile_) ||
                p.extension_keyword_of(tokens[t]) ==
                    extension_keyword::restrict ||
                p.extension_keyword_of(tokens[t]) == extension_keyword::atomic)
                return ill_formed_at_token(
                    t,
                    "a friend declaration that does not declare a function "
                    "has no cv-qualifier",
                    "class.friend");
            /* [class.friend]/8 (and [dcl.stc]/1's storage-class-specifiers,
               typedef, inline, constexpr: no form of /3 has them) */
            standard_keyword k = p.keyword_of(tokens[t]);
            if (k != standard_keyword::none && k != standard_keyword::friend_ &&
                k != standard_keyword::typename_ && !is_simple_type_keyword(k))
                return ill_formed_at_token(
                    t,
                    "a friend declaration that does not declare a function "
                    "has only friend and a type specifier",
                    "class.friend");
        } else if (x.is(syntax_kind::elaborated_type_specifier)) {
            elaborated = x;
        }
    }
    if (elaborated)
        return friend_elaborated_class(c, n, elaborated);
    /* friend simple-type-specifier ; or friend typename-specifier ;: the
       type is analysed as any decl-specifier-seq's */
    specified s;
    bool outer = friend_allowed;
    friend_allowed = true;
    analysis_result r = decl_specifier_seq(seq, s, false);
    friend_allowed = outer;
    if (r != analysis_result::well_formed)
        return r;
    if (!s.friend_ || !s.has_type || s.declares_class || s.declares_enumeration)
        return not_supported(n, "class.friend");
    const struct type &t = type_table[s.type.unqualified()];
    /* "otherwise, the friend declaration is ignored" (friend int;, a
       template's friend T; with T = int) */
    if (t.kind != type_kind::class_type ||
        entity_table[t.entity].kind != entity_kind::class_type)
        return analysis_result::well_formed;
    befriend(c, t.entity);
    return analysis_result::well_formed;
}

/*
 * friend class-key ... ;  [class.friend]/2, /3 with an
 * elaborated-type-specifier.  [namespace.memdef]/3 (C++20 [namespace.memdef]
 * /3, [dcl.meaning]): "If the name in a friend declaration is neither
 * qualified nor a template-id and the declaration is a function or an
 * elaborated-type-specifier, the lookup to determine whether the entity
 * has been previously declared shall not consider any scopes outside the
 * innermost enclosing namespace."  /11: in a local class, not outside the
 * innermost enclosing non-class scope; with no prior declaration "the class
 * that is specified belongs to the innermost enclosing non-class scope".
 */
analysis_result semantic_analysis::friend_elaborated_class(entity_index c,
                                                           SyntaxRef n,
                                                           SyntaxElement type)
{
    standard_keyword key = leading_keyword(type);
    if (!is_class_key(key))
        return not_supported(n, "class.friend");
    if (type.size() == 3) {
        /* a qualified name: [basic.lookup.elab]/3, a class declared
           before */
        type_index named = 0;
        VTRY(r, elaborated_type_specifier(type, named));
        const struct type &record = type_table[named];
        if (record.kind != type_kind::class_type ||
            entity_table[record.entity].kind != entity_kind::class_type)
            return not_supported(n, "class.friend");
        befriend(c, record.entity);
        return analysis_result::well_formed;
    }
    if (type.size() == 2 && type[1].is(syntax_kind::simple_template_id)) {
        /* [temp.friend]/1: "A friend of a class or class template can be
           ... a specialization of a function template or class template":
           the specialization named */
        type_index named = 0;
        uint32_t cv = 0;
        declaring_specializations++;
        analysis_result r = template_type(type[1], 0, false, named, &cv);
        declaring_specializations--;
        if (r != analysis_result::well_formed)
            return r;
        const struct type &record = type_table[named];
        if (record.kind != type_kind::class_type ||
            entity_table[record.entity].kind != entity_kind::class_type ||
            entity_table[record.entity].union_ !=
                (key == standard_keyword::union_))
            return not_supported(n, "class.friend");
        befriend(c, record.entity);
        return analysis_result::well_formed;
    }
    if (type.size() != 2 || !type[1].is_identifier())
        return not_supported(n, "class.friend");
    uint32_t name = tokens.identifier(type[1].token());
    entity_index local = local_scope_of(c);
    /* the scope the prior declaration is looked for in, and the one a
       class first declared here belongs to */
    entity_index space = local;
    if (!space)
        space = enclosing_namespace(c);
    if (!local && entity_table[space].unknown_names)
        return not_supported(n, "class.friend");
    lookup_result found = lookup_unqualified(name, lookup_filter::types);
    if (found.unknown || found.ambiguous)
        return not_supported(n, "class.friend");
    if (local && found.entity && is_namespace(found.found_in))
        /* /11: "a prior declaration is looked up without considering
           scopes that are outside the innermost enclosing non-class
           scope" (friend class X; in a local class is a local X, not ::X) */
        found.entity = 0;
    if (found.entity) {
        const entity &x = entity_table[found.entity];
        /* [dcl.type.elab]/2: "If the identifier ... resolves to a
           typedef-name ..., the elaborated-type-specifier is ill-formed";
           a template-name is no class-name either */
        if (x.kind == entity_kind::typedef_name ||
            x.kind == entity_kind::template_entity)
            return ill_formed(type,
                              "the identifier of an elaborated-type-specifier "
                              "resolves to a class-name",
                              "dcl.type.elab");
        bool outside = is_namespace(found.found_in) &&
                       found.found_in != space &&
                       namespace_encloses(found.found_in, space);
        /* the scopes lookup considers: the innermost enclosing namespace
           (or block, /11) and the classes inside it, c and those around
           c (friend class Nested; names c's own member class) */
        bool considered = !is_namespace(found.found_in) ||
                          (x.enclosing == space && found.found_in == space &&
                           !entity_table.bound_by_using(space, name));
        if (x.kind == entity_kind::class_type && considered) {
            /* [dcl.type.elab]/3: "the union class-key shall be used to
               refer to a union, and either the class or struct class-key
               shall be used to refer to a non-union class" */
            if (x.union_ != (key == standard_keyword::union_))
                return ill_formed(type,
                                  "the class-key of an elaborated-type-"
                                  "specifier agrees in kind with the class",
                                  "dcl.type.elab");
            befriend(c, found.entity);
            return analysis_result::well_formed;
        }
        /* a name brought by a using-declaration or directive, an entity
           that is no class */
        if (!outside || local || tokens.files.behavior.microsoft_extensions)
            return not_supported(n, "class.friend");
    }
    /* [ext.microsoft] an unqualified friend class's name is looked up
       outside the innermost enclosing namespace */
    if (tokens.files.behavior.microsoft_extensions)
        return not_supported(n, "class.friend");
    /* no prior declaration in the scope: the class of that name the scope
       declares later ([namespace.memdef]/3, /11: lookup does not find it
       until then) */
    befriend_by_name(c, space, name);
    return analysis_result::well_formed;
}

/* ---- a friend function: [class.friend]/4 to /8 --------------------------- */

/* [class.friend]/5: "When a friend declaration refers to an overloaded name
   or operator, only the function specified by the parameter types becomes a
   friend": the function of the overload set starting at first whose
   parameter-type-list is type's, 0 if none */
entity_index
semantic_analysis::overload_with_parameters(entity_index first,
                                            qualified_type type) const
{
    for (entity_index o = first;
         o && entity_table[o].kind == entity_kind::function;
         o = entity_table[o].next_overload)
        if (same_parameters(type_table, entity_table[o].type.unqualified(),
                            type.unqualified()))
            return o;
    return 0;
}

/* a function of block_declared whose name and parameter-type-list are
   these, in namespace scope, 0 if none: one declared by an earlier friend
   declaration or block-scope declaration, not bound in its namespace */
entity_index semantic_analysis::declared_unbound(entity_index scope,
                                                 uint32_t name,
                                                 qualified_type type) const
{
    for (entity_index e : block_declared) {
        const entity &x = entity_table[e];
        if (x.enclosing == scope && x.name == name &&
            x.kind == entity_kind::function &&
            same_parameters(type_table, x.type.unqualified(),
                            type.unqualified()))
            return e;
    }
    return 0;
}

/*
 * A friend function declaration or definition in class c:
 *
 *   /8: "No storage-class-specifier shall appear in the decl-specifier-seq
 *   of a friend declaration."
 *   /6: "A function can be defined in a friend declaration of a class if
 *   and only if the class is a non-local class, the function name is
 *   unqualified, and the function has namespace scope."
 *   /4: "A function first declared in a friend declaration has the linkage
 *   of the namespace of which it is a member ...  Otherwise, the function
 *   retains its previous linkage."  [namespace.memdef]/3: it is a member of
 *   the innermost enclosing namespace, not found by ordinary lookup until
 *   declared there (argument-dependent lookup finds it,
 *   [basic.lookup.argdep]/4.3).
 *   /5: of an overload set, the function with these parameter types.
 *   /7: "Such a function is implicitly an inline function if it is
 *   attached to the global module.  A friend function defined in a class is
 *   in the (lexical) scope of the class in which it is defined": its body
 *   is analysed with the class's members (friend_function_body) after the
 *   class is complete, as a member function's (bodies).
 *   /11: in a local class, local_friend_function.
 *
 * The declarator chooses: a qualified name is a member of another class
 * (friend_member_function) or of a namespace (qualified_friend_function).
 */
analysis_result
semantic_analysis::friend_function(entity_index c, SyntaxRef n,
                                   owned_array<SyntaxRef> &bodies)
{
    /* [ext:gnu_attributes] before it all: GCC's, which a friend's
       definition has no rules for beyond its form (visibility as the
       class's members') */
    uint32_t leading = leading_gnu_attributes(n);
    SyntaxTail tail(n, leading);
    uint32_t count = n.size() - leading;
    bool defines = n.is(syntax_kind::function_definition);
    entity_index local = local_scope_of(c);
    /* a requires-clause between the declarator and the body (the
       trailing one of [dcl.decl]/1) */
    SyntaxElement requires_clause = {};
    if (defines && count == 4 && tail[2].is(syntax_kind::requires_clause))
        requires_clause = tail[2];
    if (defines) {
        auto body = n.function_definition().function_body();
        if ((count != 3 && !requires_clause) ||
            !tail[0].is(syntax_kind::decl_specifier_seq) ||
            !body.is(syntax_kind::function_body))
            return not_supported(n, "class.friend");
        if (!body.defaulted() && !body.plain())
            return not_supported(n, "dcl.fct.def.general");
        /* /6: "if and only if the class is a non-local class" */
        if (local)
            return ill_formed(n,
                              "a function cannot be defined in a friend "
                              "declaration of a local class",
                              "class.friend");
    } else if (n.is(syntax_kind::member_declaration) && count == 4 &&
               tail[2].is(syntax_kind::requires_clause)) {
        /* [temp.friend]/9: "A non-template friend declaration with a
           requires-clause shall be a definition." (in a class that is not
           templated, [dcl.decl]/4 forbids the requires-clause too) */
        return ill_formed(tail[2],
                          "a non-template friend declaration with a "
                          "requires-clause shall be a definition",
                          "temp.friend");
    } else if (!n.is(syntax_kind::member_declaration) || count != 3 ||
               !tail[2].is(preprocessing_op_or_punc::SEMICOLON)) {
        return not_supported(n, "class.friend");
    } else if (tail[1].is(syntax_kind::member_declarator,
                          syntax_kind::member_declarator_list)) {
        /* [temp.friend]/9: "A non-template friend declaration with a
           requires-clause shall be a definition." (the other parts of a
           member-declarator, an initializer or a bit-field width, no
           friend function has either) */
        for (SyntaxElement x : tail[1].elements())
            if (x.is(syntax_kind::requires_clause))
                return ill_formed(x,
                                  "a non-template friend declaration with a "
                                  "requires-clause shall be a definition",
                                  "temp.friend");
        return not_supported(n, "class.friend");
    }
    specified s;
    bool outer = friend_allowed;
    friend_allowed = true;
    /* [dcl.spec.auto]/3: a friend function's
       return type can be a placeholder */
    placeholder_allowed = true;
    analysis_result r = decl_specifier_seq(tail[0], s, false);
    friend_allowed = outer;
    if (r != analysis_result::well_formed)
        return r;
    if (!s.friend_ || s.typedef_ || s.declares_class || s.declares_enumeration)
        return not_supported(n, "class.friend");
    /* /8 */
    if (s.static_ || s.extern_ || s.mutable_ || s.c11_thread_local ||
        s.cxx_thread_local)
        return ill_formed(tail[0],
                          "no storage-class-specifier shall appear in the "
                          "decl-specifier-seq of a friend declaration",
                          "class.friend");
    /* [dcl.fct.spec]/2: "The virtual specifier shall be used only in the
       initial declaration of a non-static member function" (a friend is
       not a member, /1) */
    if (s.virtual_)
        return ill_formed(tail[0],
                          "virtual only in the declaration of a member "
                          "function",
                          "dcl.fct.spec");
    /* a constructor or destructor of another class (friend A::A();) has no
       type in its decl-specifier-seq */
    bool no_type = !s.has_type;
    qualified_type type = s.type;
    if (no_type)
        type = qualified_type::make(
            type_table.fundamental(fundamental_type::void_), cv_none);
    /* [temp.friend]/1, [temp.arg.explicit]: friend R f<>(P); names the
       specialization of the function template f the declared type picks
       (as an explicit instantiation declaration names it): it is the
       friend */
    if (!defines && !no_type &&
        DeclaratorParts::of_declaration(n).id.is(
            syntax_kind::simple_template_id)) {
        uint32_t index = 0;
        specified named;
        scoped_value<bool> allowed(friend_allowed, true);
        VTRY_SET(r, named_specialization(n, n, leading, false, true, named,
                                         index, c));
        friend_record f;
        f.granting = c;
        f.befriended = instantiations[index].specialization;
        friends.append(f);
        return analysis_result::well_formed;
    }
    declarator_meaning m;
    function_cv = 0;
    function_parameters.clear();
    entity_index class_before = defining_class;
    friend_declarator = true;
    r = declarator(n, tail[1], type, m);
    friend_declarator = false;
    defining_class = class_before;
    if (r != analysis_result::well_formed)
        return r;
    VTRY_SET(r, misplaced_function_qualifiers(n, type, false));
    /* /6: "the function name is unqualified" */
    if (defines && (m.qualifier || m.qualified_namespace))
        return ill_formed_at_token(m.id,
                                   "a function defined in a friend "
                                   "declaration has an unqualified name",
                                   "class.friend");
    if (m.qualified_namespace && !m.qualifier) {
        if (!m.has_id || m.operator_function || m.destructor ||
            m.conversion_function || m.literal_operator || m.aligned ||
            m.gnu_flags || function_cv || c_language ||
            type_table[type.unqualified()].kind != type_kind::function)
            return not_supported(n, "class.friend");
        return qualified_friend_function(c, n, m, type);
    }
    if (m.qualifier && m.has_id)
        return friend_member_function(c, n, m, type, no_type, s.constexpr_);
    if (no_type)
        return not_supported(n, "class.friend");
    /* an operator function ([over.oper]: its declaration's checks are
       operator_function_declaration's), not an allocation function */
    if (!m.has_id || m.qualifier || m.qualified_namespace ||
        (m.operator_function && m.allocation != allocation_function::none) ||
        m.literal_operator || m.destructor || m.aligned || m.gnu_flags ||
        function_cv || c_language ||
        type_table[type.unqualified()].kind != type_kind::function)
        return not_supported(n, "class.friend");
    for (const parameter_meaning &parameter : function_parameters)
        if (parameter.default_argument)
            return not_supported(n, "dcl.fct.default");
    uint32_t name = tokens.identifier(m.id);
    if (defines && explicit_body(tail[count - 1], standard_keyword::default_) &&
        !defaulted_equality_declared(c, name, type))
        return not_supported(n, "class.compare.default");
    if (local)
        return local_friend_function(c, n, m, type);
    if (requires_clause)
        return constrained_friend_function(c, n, m, type, s, requires_clause,
                                           bodies);
    entity_index space = enclosing_namespace(c);
    if (entity_table[space].unknown_names ||
        entity_table.bound_by_using(space, name))
        return not_supported(n, "class.friend");
    entity_index befriended = 0;
    if (entity_index prior = entity_table.find(space, name)) {
        if (entity_table[prior].kind == entity_kind::none &&
            entity_table[prior].held_by_template &&
            entity_table[prior].enclosing == space) {
            /* [temp.over]/1: the name is held by a function template of
               the namespace; the non-template functions of the name are
               in template_name_functions (as a declaration finds them) */
            for (const template_name_function &x : template_name_functions)
                if (x.name == prior && !befriended &&
                    same_parameters(type_table,
                                    entity_table[x.function].type.unqualified(),
                                    type.unqualified()))
                    befriended = x.function;
        } else if (entity_table[prior].kind != entity_kind::function) {
            return not_supported(n, "class.friend");
        }
        if (!befriended)
            befriended = overload_with_parameters(prior, type);
    }
    if (!befriended)
        befriended = declared_unbound(space, name, type);
    if (befriended) {
        /* /4: "Otherwise, the function retains its previous linkage";
           [over.load]/2, [dcl.link]: a return type or language linkage of
           its own is not analysed */
        if (!(entity_table[befriended].type == type) ||
            entity_table[befriended].c_language_linkage)
            return not_supported(n, "class.friend");
    } else {
        if (type_without_external_linkage(type) ||
            tokens.files.behavior.microsoft_extensions)
            return not_supported(n, "class.friend");
        /* /4: first declared here, it has the linkage of its namespace */
        befriended = entity_table.make(entity_kind::function, name, space);
        entity &made = entity_table[befriended];
        made.type = type;
        made.implemented = m.implemented;
        made.operator_function = m.operator_function;
        made.linkage_of_name = linkage::external;
        /* [basic.link]/4: within an unnamed namespace, internal */
        if (within_unnamed_namespace(space))
            made.linkage_of_name = linkage::internal;
        block_declared.append(befriended);
    }
    return befriend_function(c, n, befriended, s, m, defines, bodies);
}

/*
 * A friend function definition with a requires-clause, friend R f(P)
 * requires E { ... }:
 *
 *   [dcl.decl]/4: "The optional requires-clause in an init-declarator or
 *   member-declarator shall be present only if the declarator declares a
 *   templated function": c is a templated entity, a class template's
 *   specialization or a class in one ([temp.pre]/8).
 *   [temp.friend]/9: "A non-template friend declaration with a
 *   requires-clause shall be a definition (friend_function checks a
 *   declaration) ... Such a constrained friend function ... declaration
 *   does not declare the same function ... as a declaration in any other
 *   scope": each specialization of the enclosing template declares a
 *   function of its own, never a redeclaration of a namespace's, found by
 *   argument-dependent lookup through c ([basic.lookup.argdep]/4.3).
 *   [over.match.viable]/3: one whose constraints are not satisfied is not
 *   viable: E is checked with c's template arguments (as a member
 *   function's, class-specifier.cpp), the function marked
 *   constraint_unsatisfied, and its body not analysed ([temp.inst]/11:
 *   not instantiated unless used, which it cannot be).
 */
analysis_result semantic_analysis::constrained_friend_function(
    entity_index c, SyntaxRef n, const declarator_meaning &m,
    qualified_type type, const specified &s, SyntaxElement requires_clause,
    owned_array<SyntaxRef> &bodies)
{
    bool templated = false;
    for (entity_index k = c; !is_namespace(k); k = entity_table[k].enclosing)
        if (entity_table[k].instantiation)
            templated = true;
    if (!templated)
        return ill_formed(requires_clause,
                          "a trailing requires-clause is present only if the "
                          "declarator declares a templated function",
                          "dcl.decl");
    if (requires_clause.size() != 2)
        return not_supported(n, "temp.constr");
    bool satisfied = true;
    VTRY(r, constraint_satisfied(requires_clause[1], current_scope,
                                 lookup_horizon, satisfied));
    entity_index space = enclosing_namespace(c);
    entity_index f = entity_table.make(entity_kind::function,
                                       tokens.identifier(m.id), space);
    entity &made = entity_table[f];
    made.type = type;
    made.implemented = m.implemented;
    made.operator_function = m.operator_function;
    made.linkage_of_name = linkage::external;
    if (within_unnamed_namespace(space))
        made.linkage_of_name = linkage::internal;
    made.constraint_unsatisfied = !satisfied;
    block_declared.append(f);
    if (!satisfied) {
        /* declared, not defined: its body is not instantiated */
        entity_table.add_declaration(f, n);
        befriend(c, f);
        return analysis_result::well_formed;
    }
    return befriend_function(c, n, f, s, m, true, bodies);
}

/*
 * What every friend declaration of a non-member function f in class c
 * does once f is known: its definition (/6, /7), the specifiers that
 * accumulate over its declarations, the declaration itself, and the
 * friendship.
 */
analysis_result semantic_analysis::befriend_function(
    entity_index c, SyntaxRef n, entity_index f, const specified &s,
    const declarator_meaning &m, bool defines, owned_array<SyntaxRef> &bodies)
{
    if (defines) {
        /* [basic.def.odr]/1: one definition */
        if (entity_table[f].defined)
            return not_supported(n, "basic.def.odr");
        /* /7: "implicitly an inline function" (no modules: every function
           is attached to the global module) */
        entity_table[f].inline_ = true;
        bodies.append(n);
        friend_definitions.append({n, f, c});
    }
    /* [dcl.inline]: a function is inline once a declaration says so;
       [dcl.constexpr]/1: constexpr implies inline, and every declaration
       of the function must have it */
    if (s.inline_)
        entity_table[f].inline_ = true;
    if (entity_table[f].first_declaration) {
        VTRY(c1, constexpr_redeclaration(m.id, f, s.constexpr_));
    }
    if (s.constexpr_) {
        entity_table[f].inline_ = true;
        entity_table[f].constexpr_ = true;
        entity_table[f].immediate = s.consteval_;
    }
    if (m.operator_function) {
        VTRY(o, operator_function_declaration(n, f));
    }
    entity_table.add_declaration(f, n);
    befriend(c, f);
    return analysis_result::well_formed;
}

/*
 * [class.friend]/11: "If a friend declaration appears in a local class and
 * the name specified is an unqualified name, a prior declaration is looked
 * up without considering scopes that are outside the innermost enclosing
 * non-class scope.  For a friend function declaration, if there is no
 * prior declaration, the program is ill-formed."  The prior declaration is
 * a block-scope function declaration (void b(); in the function), which
 * declares a function of the namespace ([basic.link]/7): that function is
 * the friend.  (Not a definition: /6 refused it.)
 */
analysis_result
semantic_analysis::local_friend_function(entity_index c, SyntaxRef n,
                                         const declarator_meaning &m,
                                         qualified_type type)
{
    uint32_t name = tokens.identifier(m.id);
    lookup_result found = lookup_unqualified(name, lookup_filter::any);
    if (found.unknown || found.ambiguous)
        return not_supported(n, "class.friend");
    entity_index befriended = 0;
    if (found.entity && !is_namespace(found.found_in) &&
        entity_table[found.entity].kind == entity_kind::function)
        befriended = overload_with_parameters(found.entity, type);
    if (!befriended)
        return ill_formed_at_token(m.id,
                                   "a friend function declared in a local "
                                   "class shall have a prior declaration in "
                                   "the innermost enclosing block",
                                   "class.friend");
    if (!(entity_table[befriended].type == type))
        return not_supported(n, "class.friend");
    entity_table.add_declaration(befriended, n);
    befriend(c, befriended);
    return analysis_result::well_formed;
}

/* what a member of class k whose type is function declares by its
   parameter-type-list ([class.copy.ctor]/1, /2, [class.copy.assign]/1): a
   copy (one parameter k&, const k&, volatile k& or const volatile k&), a
   move (k&& and its cv forms) or neither */
semantic_analysis::copy_kind
semantic_analysis::copy_parameter(entity_index k, qualified_type function) const
{
    const struct type &f = type_table[function.unqualified()];
    if (f.kind != type_kind::function || f.parameter_count != 1 || f.variadic)
        return copy_kind::none;
    qualified_type p = type_table.parameters(function.unqualified())[0];
    const struct type &r = type_table[p.unqualified()];
    if (r.kind != type_kind::lvalue_reference &&
        r.kind != type_kind::rvalue_reference)
        return copy_kind::none;
    const struct type &object = type_table[r.operand.unqualified()];
    if (object.kind != type_kind::class_type || object.entity != k)
        return copy_kind::none;
    if (r.kind == type_kind::rvalue_reference)
        return copy_kind::move;
    return copy_kind::copy;
}

/*
 * [class.friend]/5's member of another class when it is a special member
 * function class k declares implicitly and analysis has not declared yet
 * (declared when a use needs it): whether k has the member the friend
 * declaration of type `type` names.  An implicitly-declared member is a
 * declared member ([dcl.meaning]/1 is met), public and inline
 * ([class.default.ctor]/1, [class.dtor]/2, [class.copy.ctor]/6, /8,
 * [class.copy.assign]/2, /4):
 *   ~K()                when K has no user-declared destructor (/4);
 *   K()                 when K has no user-declared constructor;
 *   K(const K&)         when K has no user-declared copy constructor;
 *   K(K&&), K& operator=(K&&)   when K has no user-declared copy
 *                       constructor, copy assignment operator, move
 *                       constructor, move assignment operator or
 *                       destructor;
 *   K& operator=(const K&)      when K has no user-declared copy
 *                       assignment operator.
 * (The parameter's cv-qualification, const K& or K&, follows the
 * subobjects' members: the const form is taken, as for every class
 * analysis declares one for.)
 */
bool semantic_analysis::implicitly_declares(entity_index k, bool destructor,
                                            bool assignment,
                                            qualified_type type)
{
    const entity &x = entity_table[k];
    bool user_destructor = x.destructor && !entity_table[x.destructor].implicit;
    if (destructor)
        return !user_destructor &&
               !type_table[type.unqualified()].parameter_count;
    bool user_constructor = false, user_copy = false, user_move = false;
    for (entity_index o = x.first_constructor; o;
         o = entity_table[o].next_constructor) {
        if (entity_table[o].implicit)
            continue;
        user_constructor = true;
        copy_kind kind = copy_parameter(k, entity_table[o].type);
        if (kind == copy_kind::copy)
            user_copy = true;
        else if (kind == copy_kind::move)
            user_move = true;
    }
    bool user_copy_assign = false, user_move_assign = false;
    uint32_t name = operator_name(preprocessing_op_or_punc::EQUAL);
    entity_index first = entity_table.find(k, name);
    for (entity_index o = first;
         o && entity_table[o].kind == entity_kind::function;
         o = entity_table[o].next_overload) {
        if (entity_table[o].implicit)
            continue;
        copy_kind kind = copy_parameter(k, entity_table[o].type);
        if (kind == copy_kind::copy)
            user_copy_assign = true;
        else if (kind == copy_kind::move)
            user_move_assign = true;
    }
    if (first && entity_table[first].kind != entity_kind::function)
        return false;
    copy_kind kind = copy_parameter(k, type);
    bool any_user = user_copy || user_copy_assign || user_move ||
                    user_move_assign || user_destructor;
    if (assignment) {
        if (kind == copy_kind::copy)
            return !user_copy_assign;
        if (kind == copy_kind::move)
            return !any_user && tokens.files.behavior.has(
                                    language_feature::rvalue_references);
        return false;
    }
    if (!type_table[type.unqualified()].parameter_count)
        return !user_constructor;
    if (kind == copy_kind::copy)
        return !user_copy;
    if (kind == copy_kind::move)
        return !any_user &&
               tokens.files.behavior.has(language_feature::rvalue_references);
    return false;
}

/*
 * [class.friend]/5: "A member function of a class X can be a friend of a
 * class Y": friend R A::f(P); friend A::A(P); friend A::~A();
 * friend R A::operator@(P);  The declaration "shall refer to a previously
 * declared member of the class to which the qualifier refers"
 * ([dcl.meaning]/1), by name, parameter-type-list and cv-qualifiers
 * ([over.load]/2, [class.mfct.non.static]); an implicitly-declared special
 * member is one (implicitly_declares).  It is that member that is the
 * friend, so its body is in a friend of c.
 */
analysis_result semantic_analysis::friend_member_function(
    entity_index c, SyntaxRef n, const declarator_meaning &m,
    qualified_type type, bool no_type, bool is_constexpr)
{
    entity_index k = m.qualifier;
    uint32_t name = tokens.identifier(m.id);
    if ((m.operator_function && m.allocation != allocation_function::none) ||
        m.literal_operator || m.conversion_function || m.aligned ||
        m.gnu_flags || c_language ||
        entity_table[k].kind != entity_kind::class_type ||
        type_table[type.unqualified()].kind != type_kind::function)
        return not_supported(n, "class.friend");
    entity_index befriended = 0;
    bool special = m.destructor || no_type;
    bool assignment =
        m.operator_function && m.implemented == preprocessing_op_or_punc::EQUAL;
    if (special) {
        if (name != entity_table[k].name || function_cv)
            return not_supported(n, "class.friend");
        if (m.destructor) {
            befriended = entity_table[k].destructor;
        } else {
            /* a constructor: by its parameters (an exception specification
               is not part of the match, [except.spec]/5 checks it) */
            for (entity_index o = entity_table[k].first_constructor; o;
                 o = entity_table[o].next_constructor)
                if (same_parameters(type_table,
                                    entity_table[o].type.unqualified(),
                                    type.unqualified()))
                    befriended = o;
        }
    } else {
        entity_index first = entity_table.find(k, name);
        if (first && entity_table[first].kind != entity_kind::function)
            return not_supported(n, "class.friend");
        for (entity_index o = first;
             o && entity_table[o].kind == entity_kind::function;
             o = entity_table[o].next_overload)
            if (same_parameters(type_table, entity_table[o].type.unqualified(),
                                type.unqualified()) &&
                entity_table[o].member_cv == function_cv)
                befriended = o;
        /* [over.load]/2: "Function declarations that differ only in the
           return type ... cannot be overloaded": the member of these
           parameters with another return type is no match */
        if (befriended &&
            !(type_table[entity_table[befriended].type.unqualified()].operand ==
              type_table[type.unqualified()].operand))
            return not_supported(n, "class.friend");
    }
    if (!befriended && (special || assignment) && entity_table[k].defined &&
        implicitly_declares(k, m.destructor, assignment, type)) {
        /* the implicitly-declared member analysis has not declared yet: it
           is public, so the friendship changes no access.  This declaration
           must agree with the implicit one: [dcl.constexpr]/1 "If any
           declaration of a function or function template has a constexpr
           ... specifier, then all its declarations shall contain the same
           specifier"; [except.spec]/4 the same for a non-throwing exception
           specification.  Known for a class whose special members are all
           trivial ([class.default.ctor]/3, [class.copy.ctor]/12,
           [class.copy.assign]/10, [class.dtor]/9: constexpr when they
           would satisfy the constexpr rules of the edition; [except.spec]/8
           non-throwing): its constructors constexpr from C++11 (the
           default constructor of a class with a data member only from
           C++20, which drops the rule that every member be initialized),
           its assignment operators from C++14, its destructor from C++20;
           all non-throwing (part of the type from C++17). */
        if (!trivial_special_members(k))
            return not_supported(n, "class.friend");
        standard_edition edition = tokens.files.behavior.edition;
        bool constructor = !m.destructor && !assignment;
        bool constexpr_implicit = false;
        if (m.destructor)
            constexpr_implicit = edition >= standard_edition::cplusplus_20;
        else if (assignment)
            constexpr_implicit = edition >= standard_edition::cplusplus_14;
        else
            constexpr_implicit =
                tokens.files.behavior.has(language_feature::rvalue_references);
        if (constructor && !type_table[type.unqualified()].parameter_count &&
            entity_table[k].member_count &&
            edition < standard_edition::cplusplus_20)
            return not_supported(n, "class.friend");
        if (is_constexpr != constexpr_implicit)
            return ill_formed_at_token(
                m.id,
                "every declaration of a function has the constexpr "
                "specifier if one does (the implicit declaration of the "
                "special member)",
                "dcl.constexpr");
        bool non_throwing = type_table[type.unqualified()].non_throwing;
        if (edition >= standard_edition::cplusplus_17 && !m.destructor &&
            !non_throwing)
            return ill_formed_at_token(
                m.id,
                "every declaration of a function has a non-throwing "
                "exception specification if one does (the implicit "
                "declaration of the special member)",
                "except.spec");
        return analysis_result::well_formed;
    }
    if (!befriended)
        return ill_formed_at_token(m.id,
                                   "when the declarator-id is qualified, the "
                                   "declaration shall refer to a previously "
                                   "declared member of the class",
                                   "dcl.meaning");
    if (!entity_table[befriended].implicit) {
        VTRY(cr, constexpr_redeclaration(m.id, befriended, is_constexpr));
        entity_table.add_declaration(befriended, n);
    }
    befriend(c, befriended);
    return analysis_result::well_formed;
}

/*
 * [class.friend]/1, [dcl.meaning]/1: a friend function named by a qualified
 * name, friend R ns::f(P);: "the declaration shall refer to a previously
 * declared member of the ... namespace to which the qualifier refers" (of an
 * element of its inline namespace set); it is that function, with its
 * return type ([over.load]/2).  /6: it is not a definition.
 */
analysis_result
semantic_analysis::qualified_friend_function(entity_index c, SyntaxRef n,
                                             const declarator_meaning &m,
                                             qualified_type type)
{
    uint32_t name = tokens.identifier(m.id);
    entity_index scope = m.namespace_qualifier;
    lookup_result found = lookup_in_namespace(scope, name, lookup_filter::any);
    if (found.ambiguous || found.unknown)
        return not_supported(n, "dcl.meaning");
    if (!found.entity)
        return ill_formed_at_token(m.id,
                                   "a qualified friend declaration shall "
                                   "refer to a previously declared member "
                                   "of the namespace",
                                   "dcl.meaning");
    entity_index prior = found.entity;
    entity_index in = entity_table[prior].enclosing;
    if (entity_table[prior].kind != entity_kind::function ||
        entity_table.find(found.found_in, name) != prior ||
        found.found_in != in)
        /* a template, or named by a using-declaration or directive */
        return not_supported(n, "class.friend");
    for (entity_index y = in; y != scope; y = entity_table[y].enclosing)
        if (!y || !entity_table[y].inline_)
            return not_supported(n, "dcl.meaning");
    entity_index befriended = overload_with_parameters(prior, type);
    if (!befriended)
        return ill_formed_at_token(m.id,
                                   "a qualified friend declaration shall "
                                   "refer to a previously declared member "
                                   "of the namespace",
                                   "dcl.meaning");
    if (!(entity_table[befriended].type == type) ||
        entity_table[befriended].c_language_linkage)
        return not_supported(n, "class.friend");
    entity_table.add_declaration(befriended, n);
    befriend(c, befriended);
    return analysis_result::well_formed;
}

/* ---- a function defined in a friend declaration: [class.friend]/6, /7 ---- */

/* the scope unqualified lookup goes to after s: its enclosing one, or for
   a friend function defined in a class, the class (/7: "in the (lexical)
   scope of the class in which it is defined") */
entity_index semantic_analysis::lexical_parent(entity_index s) const
{
    if (entity_table[s].kind == entity_kind::function &&
        entity_table[s].inline_)
        for (const friend_definition &d : friend_definitions)
            if (d.function == s)
                return d.granting;
    return entity_table[s].enclosing;
}

bool semantic_analysis::defined_by_friend(entity_index f) const
{
    for (const friend_definition &d : friend_definitions)
        if (d.function == f)
            return true;
    return false;
}

/* the function a friend definition n in class c defines (a class
   template's defines one per specialization, [temp.friend]), 0 if n is
   not one */
entity_index semantic_analysis::friend_defined_by(SyntaxRef n,
                                                  entity_index c) const
{
    for (const friend_definition &d : friend_definitions)
        if (d.definition == n && d.granting == c)
            return d.function;
    return 0;
}

/* /7: the body of a friend defined in class c, in the class's scope: its
   declarator again for the parameters, then the body */
analysis_result semantic_analysis::friend_function_body(
    entity_index c, FunctionDefinition definition, entity_index f)
{
    current_declaration = definition;
    entity_index scope = current_scope;
    current_scope = c;
    specified s;
    bool outer = friend_allowed;
    friend_allowed = true;
    placeholder_allowed = true;
    analysis_result r =
        decl_specifier_seq(definition.decl_specifier_seq().index(), s, false);
    friend_allowed = outer;
    qualified_type type = s.type;
    declarator_meaning m;
    if (r == analysis_result::well_formed) {
        function_cv = 0;
        function_parameters.clear();
        r = declarator(definition, definition.declarator(), type, m);
        if (r == analysis_result::well_formed)
            r = misplaced_function_qualifiers(definition, type, false);
    }
    if (r == analysis_result::well_formed) {
        last_declared = f;
        auto body = definition.function_body();
        if (body.defaulted()) {
            scoped_value<entity_index> compared(defaulted_comparison, c);
            r = analyse_function_body(definition, body);
        } else {
            r = analyse_function_body(definition, body.compound_statement());
        }
    }
    current_scope = scope;
    return r;
}

} // namespace cplusplus
