/*
 * structured-binding.cpp: [dcl.struct.bind] Structured binding declarations,
 * the third alternative of [dcl.pre]/1's
 *
 *     simple-declaration:
 *         attribute-specifier-seq_opt decl-specifier-seq ref-qualifier_opt [ identifier-list ] initializer ;
 *
 * and of [stmt.iter]'s
 *
 *     for-range-declaration:
 *         attribute-specifier-seq_opt decl-specifier-seq ref-qualifier_opt [ identifier-list ]
 *
 * with [cpp.pre]'s
 *
 *     identifier-list:
 *         identifier
 *         identifier-list , identifier
 *
 * [dcl.struct.bind]/1: "A structured binding declaration introduces the
 * identifiers v0, v1, v2, ... of the identifier-list as names
 * ([basic.scope.declarative]) of structured bindings."  Which
 * decl-specifiers it may have is a semantic rule, [dcl.pre]/8: "If the
 * decl-specifier-seq contains any decl-specifier other than static,
 * thread_local, auto ([dcl.spec.auto]), or cv-qualifiers, the program is
 * ill-formed."
 *
 * Analysed: the variable e (/1), for each of /3 (arrays), /4 (tuple-like
 * types: std::tuple_size, get<i> found in E's associated namespaces,
 * std::tuple_element) and /5 (classes); a structured binding named
 * (structured_binding_use).  e of static storage duration (static, or at
 * namespace scope) for /3 with a ref-qualifier and /5 of E's own public
 * members.
 *
 * Not analysed: thread_local; with static storage duration an array copied
 * (/1), /4, and /5's members of a base or not public; /4's member get (a
 * member template, [temp.mem]) and a get only a hidden friend declares; a
 * structured binding of automatic storage duration named in a
 * lambda-expression ([expr.prim.lambda.capture]); /5's classes with
 * anonymous union members or virtual bases; C++26's constexpr.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* ref-qualifier_opt [ after a decl-specifier-seq: no declarator starts
   with [ ([dcl.attr.grammar]/7: [[ is an attribute-specifier's) */
bool parser::structured_binding_follows()
{
    unsigned i = 0;
    if (at(preprocessing_op_or_punc::AMPERSAND) ||
        at(preprocessing_op_or_punc::AMPERSAND_AMPERSAND))
        i = 1;
    return at(preprocessing_op_or_punc::LEFT_BRACKET, i) &&
           !at(preprocessing_op_or_punc::LEFT_BRACKET, i + 1);
}

/*
 * ref-qualifier_opt [ identifier-list ]: the tokens are added to the
 * caller's node, the identifier-list is a node when it has more than one
 * identifier.  Structured bindings are C++17's (P0217R3); in an earlier
 * edition they are clang's extension, P9's.
 */
node_index parser::structured_binding_identifiers()
{
    if (!tokens.files.behavior.has(language_feature::structured_bindings))
        return beyond_this_step();
    if (!accept(preprocessing_op_or_punc::AMPERSAND))
        accept(preprocessing_op_or_punc::AMPERSAND_AMPERSAND);
    add_token(); /* [ */
    size_t mark = begin_node();
    token_index first = position;
    unsigned count = 0;
    /* [basic.scope.pdecl]/10: "The point of declaration of a structured
       binding is immediately after the identifier-list of the structured
       binding declaration." */
    for (;;) {
        if (!at_identifier())
            return expected("an identifier", "dcl.struct.bind");
        names.declare(tokens.identifier(position), name_kind::other);
        add_token();
        count++;
        if (!accept(preprocessing_op_or_punc::COMMA))
            break;
    }
    if (count > 1)
        finish_node(syntax_kind::identifier_list, mark, first);
    if (!accept(preprocessing_op_or_punc::RIGHT_BRACKET))
        return expected("']' to end the identifier-list", "dcl.struct.bind");
    return 0;
}

/* ---- semantic analysis ---------------------------------------------- */

/* /5's members: the non-static data members, not a base subobject or an
   unnamed bit-field (which is not a member, [class.bit]/2) */
static bool binding_data_member(const entity &m)
{
    return !m.base_subobject && !(m.bit_field && !m.name);
}

/* /3: element i of the array object, "*(e + i)" by built-in [expr.sub]:
   object becomes the element */
analysis_result semantic_analysis::array_element(SyntaxRef n, token_index at,
                                                 expression_meaning &object,
                                                 uint64_t i)
{
    expression_meaning index =
        integer_constant(fundamental_type::unsigned_long_int, i, at);
    expression_meaning sum;
    analysis_result r = builtin_binary(n, at, preprocessing_op_or_punc::PLUS,
                                       object, index, sum);
    if (r == analysis_result::well_formed)
        r = indirection_of(n, at, SyntaxElement(), sum);
    object = sum;
    return r;
}

analysis_result semantic_analysis::binding_variable(SyntaxRef n,
                                                    expression_meaning &m,
                                                    qualified_type type,
                                                    bool named, bool direct,
                                                    bool list, entity_index &v)
{
    v = entity_table.make(entity_kind::variable, 0, current_scope);
    entity_table[v].defined = true;
    entity_table[v].automatic = binding_storage == linkage::none;
    if (is_namespace(current_scope))
        entity_table[v].linkage_of_name = binding_storage;
    initialized_value value;
    analysis_result r = analysis_result::well_formed;
    if (contains_placeholder(type)) {
        scoped_value<qualified_type> deduced(declaration_deduced,
                                             qualified_type());
        r = deduced_initialization(n, m, named, direct, list, v, type, value);
    } else {
        entity_table[v].type = type;
        r = initialize_with(n, m, type, direct, list, value);
    }
    if (r != analysis_result::well_formed)
        return r;
    entity_table[v].definition = value.node;
    if (function_state.active) {
        block_variables.append(v);
    } else {
        /* of namespace scope: lowering defines it, the dump claims its
           symbol (no name to claim a type for) */
        definition_record d;
        d.declaration = current_declaration;
        d.entity = v;
        definitions.append(d);
        declared_name name;
        name.declaration = n;
        name.outermost = current_declaration;
        name.entity = v;
        name.kind = entity_kind::variable;
        name.type = entity_table[v].type;
        name.claimable = false;
        declared_names.append(name);
    }
    return analysis_result::well_formed;
}

/*
 * [dcl.struct.bind]/1: "If the assignment-expression in the initializer has
 * array type A and no ref-qualifier is present, e is defined by
 *
 *     attribute-specifier-seq_opt S cv A e ;
 *
 * and each element is copy-initialized or direct-initialized from the
 * corresponding element of the assignment-expression as specified by the
 * form of the initializer."  The array m is evaluated once: an unnamed
 * reference bound to it, whose elements initialize e's.
 */
analysis_result semantic_analysis::array_binding_object(
    SyntaxRef n, token_index at, expression_meaning &m, const specified &s,
    bool direct, entity_index &e)
{
    entity_index from = 0;
    /* (automatic only: structured_binding_declaration) */
    VTRY(r, binding_variable(n, m, reference_to(placeholder_type(false), true),
                             false, false, false, from));
    qualified_type array = add_cv(
        qualified_type::make(m.type.unqualified(), m.type.cv()), s.type.cv());
    const struct type a = type_table[array.unqualified()];
    qualified_type element = a.operand;
    owned_array<uint32_t> elements;
    for (uint64_t i = 0; i < a.bound; i++) {
        expression_meaning x;
        r = named_entity(at, from, x, false);
        if (r == analysis_result::well_formed)
            r = array_element(n, at, x, i);
        initialized_value value;
        if (r == analysis_result::well_formed)
            VTRY_SET(r, initialize_with(n, x, element, direct, false, value));
        elements.append(value.node);
    }
    expression_meaning list;
    list.type = array;
    list.category = value_category::prvalue;
    list.evaluation = constness::not_constant;
    list.node = expression_node(semantic_kind::initializer_list, list, at);
    semantics.set_list(list.node, elements.data(), elements.size());
    e = entity_table.make(entity_kind::variable, 0, current_scope);
    entity_table[e].defined = true;
    entity_table[e].automatic = true;
    entity_table[e].type = array;
    entity_table[e].definition = list.node;
    block_variables.append(e);
    return analysis_result::well_formed;
}

/* std::NAME, found as [namespace.qual] finds it (in an inline namespace
   of std too) */
analysis_result semantic_analysis::std_class_template(SyntaxRef n,
                                                      const char *name,
                                                      uint32_t &template_index)
{
    template_index = 0;
    entity_index std_ =
        entity_table.find(0, tokens.pp.identifier("std", 3) + 1);
    if (!std_ || !is_namespace(std_) || !std_namespace(std_))
        return analysis_result::well_formed;
    lookup_result found = lookup_in_namespace(
        std_, tokens.pp.identifier(name, strlen(name)) + 1, lookup_filter::any);
    /* a class template's name is held with no entity of its own
       (unknown_entity); a name of a declaration analysis did not follow
       is none (tuple_size_of looks for those) */
    if (found.ambiguous)
        return not_supported_because(
            n, "namespace.qual",
            "an ambiguous std name for the tuple-like protocol");
    entity_index held = found.entity;
    if (found.unknown)
        held = found.unknown_entity;
    if (held)
        template_index = class_template_of(held);
    return analysis_result::well_formed;
}

/* /4: get<i> and std::tuple_element<i, E>'s argument i, "an index
   prvalue of type std::size_t", for the first parameter of the template
   (a non-type one of integral type) */
analysis_result semantic_analysis::index_argument(SyntaxRef n,
                                                  uint32_t template_index,
                                                  uint32_t i,
                                                  template_argument &out)
{
    const function_template ft = function_templates[template_index - 1];
    if (!ft.parameter_count)
        return not_supported_because(n, "dcl.struct.bind",
                                     "a std trait template with no parameters");
    entity_index p = template_parameter_entities[ft.first_parameter];
    const entity &x = entity_table[p];
    if (x.kind == entity_kind::typedef_name ||
        x.kind == entity_kind::template_entity ||
        template_parameter_is_pack(ft.first_parameter) ||
        depends_on_template_parameter(x.type) || !is_integral(x.type))
        return not_supported_because(
            n, "dcl.struct.bind",
            "a std trait whose index parameter is not a plain integer");
    out = template_argument();
    out.kind = template_argument::kind_t::value;
    out.type = qualified_type::make(x.type.unqualified(), cv_none);
    out.value = i;
    return analysis_result::well_formed;
}

/*
 * /4's std::tuple_size<E> and std::tuple_element<i, E> (indexed): the
 * member named member of the specialization, accessible, or 0 when the
 * template is not declared, the specialization is not complete or has no
 * such member
 */
analysis_result semantic_analysis::tuple_trait_member(
    SyntaxRef n, const char *trait, bool indexed, uint32_t i,
    qualified_type type, const char *member, entity_index &found)
{
    found = 0;
    uint32_t ct = 0;
    analysis_result r = std_class_template(n, trait, ct);
    if (r != analysis_result::well_formed || !ct)
        return r;
    const function_template ft = function_templates[ct - 1];
    uint32_t count = 1;
    if (indexed)
        count = 2;
    if (ft.parameter_count != count ||
        entity_table[template_parameter_entities[ft.first_parameter + count -
                                                 1]]
                .kind != entity_kind::typedef_name)
        return not_supported_because(
            n, "dcl.struct.bind",
            "a std tuple trait with unexpected parameters");
    template_argument a[2];
    if (indexed) {
        VTRY_SET(r, index_argument(n, ct, i, a[0]));
    }
    a[count - 1].type = type;
    entity_index c = 0;
    VTRY_SET(r, instantiate_class(n, ct, a, count, c));
    require_complete(c);
    if (entity_table[c].kind != entity_kind::class_type ||
        !entity_table[c].size)
        return analysis_result::well_formed;
    bool ambiguous = false;
    found = class_member_lookup(
        c, tokens.pp.identifier(member, strlen(member)) + 1, ambiguous);
    if (ambiguous)
        return not_supported_because(
            n, "class.member.lookup",
            "an ambiguous member of a std trait specialization");
    if (found && !accessible(found))
        return not_supported_because(
            n, "dcl.struct.bind",
            "an inaccessible member of a std trait specialization");
    return analysis_result::well_formed;
}

/*
 * /4: "if the qualified-id std::tuple_size<E> names a complete class type
 * with a member named value, the expression std::tuple_size<E>::value
 * shall be a well-formed integral constant expression"
 */
analysis_result semantic_analysis::tuple_size_of(SyntaxRef n,
                                                 qualified_type type,
                                                 bool &found, uint64_t &value)
{
    found = false;
    entity_index v = 0;
    VTRY(r, tuple_trait_member(n, "tuple_size", false, 0, type, "value", v));
    if (!v) {
        /* not complete, or no member value: a specialization of
           std::tuple_size analysis did not follow may give it one */
        uint32_t name = tokens.pp.identifier("tuple_size", 10) + 1;
        for (const analysed_declaration &d : declarations) {
            if (d.result == analysis_result::well_formed)
                continue;
            for (token_index t = d.node.first_token(); t < d.node.end_token();
                 t++)
                if (tokens[t].kind == token_kind::identifier &&
                    tokens.identifier(t) == name)
                    return not_supported_because(
                        n, "temp.expl.spec",
                        "a std trait specialization that was not analysed");
        }
        return analysis_result::well_formed;
    }
    found = true;
    const entity &x = entity_table[v];
    if (x.kind != entity_kind::variable || x.nonstatic_member ||
        !is_integral(x.type) || x.usable != constness::constant)
        return not_supported_because(
            n, "dcl.struct.bind",
            "a trait value that is no constant integral static member");
    value = x.value;
    return analysis_result::well_formed;
}

/* /4: "the type Ti designated by std::tuple_element<i, E>::type" */
analysis_result semantic_analysis::tuple_element_of(SyntaxRef n, uint32_t i,
                                                    qualified_type type,
                                                    qualified_type &element)
{
    entity_index t = 0;
    VTRY(r, tuple_trait_member(n, "tuple_element", true, i, type, "type", t));
    if (!t || entity_table[t].kind != entity_kind::typedef_name ||
        depends_on_template_parameter(entity_table[t].type))
        return not_supported_because(
            n, "dcl.struct.bind", "a tuple_element with no usable type member");
    element = entity_table[t].type;
    return analysis_result::well_formed;
}

/*
 * /4: "The unqualified-id get is looked up in the scope of E by class
 * member access lookup ([basic.lookup.classref]), and if that finds at
 * least one declaration that is a function template whose first template
 * parameter is a non-type parameter, the initializer is e.get<i>().
 * Otherwise, the initializer is get<i>(e), where get is looked up in the
 * associated namespaces ([basic.lookup.argdep]).  In either case, get<i> is
 * interpreted as a template-id. ... In either case, e is an lvalue if the
 * type of the entity e is an lvalue reference and an xvalue otherwise."
 * The function templates of the name are candidates with the explicit
 * template argument i ([temp.over]/1).
 */
analysis_result semantic_analysis::binding_get_call(SyntaxRef n, token_index at,
                                                    uint32_t i, entity_index e,
                                                    qualified_type type,
                                                    expression_meaning &m)
{
    uint32_t name = tokens.pp.identifier("get", 3) + 1;
    entity_index c = type_table[type.unqualified()].entity;
    bool ambiguous = false;
    entity_index member = class_member_lookup(c, name, ambiguous);
    /* a member template ([temp.mem]): not analysed */
    if (ambiguous ||
        (member && entity_table[member].kind != entity_kind::function))
        return not_supported_because(
            n, "dcl.struct.bind",
            "a get member that is ambiguous or no function");
    owned_array<qualified_type> types;
    types.append(type);
    owned_array<entity_index> classes, namespaces;
    if (!associated_namespaces(types, classes, namespaces))
        return not_supported_because(n, "basic.lookup.argdep",
                                     "get with unknown associated namespaces");
    entity_index held = 0;
    for (entity_index s : namespaces) {
        entity_index x = entity_table.find(s, name);
        if (!x)
            continue;
        /* a function that is no template, a second namespace's get */
        if (!function_template_of(x) || held)
            return not_supported_because(
                n, "dcl.struct.bind",
                "a get that is no single function template");
        held = x;
    }
    /* none: a hidden friend may declare it ([basic.lookup.argdep]/4.3) */
    if (!held)
        return not_supported_because(
            n, "dcl.struct.bind", "no get found by argument-dependent lookup");
    expression_meaning object;
    VTRY(r, named_entity(at, e, object, false));
    if (type_table[entity_table[e].type.unqualified()].kind !=
        type_kind::lvalue_reference) {
        object.category = value_category::xvalue;
        object.node = expression_node(semantic_kind::entity, object, at, e);
    }
    owned_array<uint32_t> group;
    function_template_overloads(held, group);
    owned_array<overload_candidate> candidates;
    owned_array<uint32_t> first_arguments;
    owned_array<template_argument> all;
    for (uint32_t g : group) {
        owned_array<template_argument> given;
        template_argument index;
        /* [temp.arg]: a template whose first parameter is a type's or a
           template's takes no value argument and is no candidate (std::get
           by type, get<T>(pair)); another that is no integral one is not
           analysed */
        const function_template f = function_templates[g - 1];
        if (f.parameter_count) {
            entity_kind first =
                entity_table[template_parameter_entities[f.first_parameter]]
                    .kind;
            if (first == entity_kind::typedef_name ||
                first == entity_kind::template_entity)
                continue;
        }
        VTRY_SET(r, index_argument(n, g, i, index));
        given.append(index);
        owned_array<uint32_t> one;
        one.append(g);
        VTRY_SET(r, template_candidates(n, one, &object, 1, candidates,
                                        &first_arguments, &all, nullptr,
                                        SyntaxRef(), &given));
    }
    if (candidates.empty())
        return not_supported_because(n, "temp.over", "no candidate for get");
    owned_array<expression_meaning> arguments;
    arguments.append(object);
    size_t chosen = 0;
    VTRY_SET(r,
             best_viable_function(n, candidates, nullptr, arguments, chosen));
    const function_template ft =
        function_templates[candidates[chosen].template_index - 1];
    entity_index specialization = 0;
    VTRY_SET(r, instantiate(n, ft.entity, &all[first_arguments[chosen]],
                            ft.parameter_count, specialization));
    return call_function(n, at, specialization, arguments, m);
}

/* /5: the number of non-static data members of class c and its bases */
uint32_t semantic_analysis::binding_member_count(entity_index c) const
{
    const entity &x = entity_table[c];
    uint32_t count = 0;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (binding_data_member(m))
            count++;
    }
    for (uint32_t i = 0; i < x.base_count; i++)
        count += binding_member_count(
            type_table[entity_table[class_bases[x.first_base + i]]
                           .type.unqualified()]
                .entity);
    return count;
}

/*
 * /5: "all of E's non-static data members shall be direct members of E or
 * of the same base class of E, ... E shall not have an anonymous union
 * member": the class whose members they are
 */
analysis_result semantic_analysis::binding_members_class(SyntaxRef n,
                                                         entity_index c,
                                                         entity_index &holder)
{
    require_complete(c);
    const entity &x = entity_table[c];
    if (x.kind != entity_kind::class_type || !x.size || x.virtual_base_count)
        return not_supported_because(
            n, "dcl.struct.bind",
            "an incomplete class or one with virtual bases");
    uint32_t own = 0;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (!binding_data_member(m))
            continue;
        /* an anonymous union (or struct) member */
        if (!m.name)
            return not_supported_because(n, "dcl.struct.bind",
                                         "an anonymous union or struct member");
        own++;
    }
    entity_index with_members = 0;
    for (uint32_t i = 0; i < x.base_count; i++) {
        entity_index b = type_table[entity_table[class_bases[x.first_base + i]]
                                        .type.unqualified()]
                             .entity;
        if (!binding_member_count(b))
            continue;
        if (own || with_members)
            return ill_formed(n,
                              "all of E's non-static data members shall be "
                              "direct members of E or of the same base "
                              "class of E",
                              "dcl.struct.bind");
        with_members = b;
    }
    if (with_members)
        return binding_members_class(n, with_members, holder);
    holder = c;
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::structured_binding_declaration(
    SyntaxRef declaration, uint32_t first, const specified &s,
    const expression_meaning *supplied)
{
    uint32_t i = first;
    bool reference = false, rvalue = false;
    if (declaration[i].is(preprocessing_op_or_punc::AMPERSAND)) {
        reference = true;
        i++;
    } else if (declaration[i].is(
                   preprocessing_op_or_punc::AMPERSAND_AMPERSAND)) {
        reference = rvalue = true;
        i++;
    }
    if (i + 3 > declaration.size())
        return not_supported_because(declaration, "dcl.struct.bind",
                                     "a structured binding with too few parts");
    token_index at = declaration[i].token();
    owned_array<token_index> names;
    SyntaxElement list = declaration[i + 1];
    if (list.is_token()) {
        names.append(list.token());
    } else {
        for (SyntaxElement c : list.elements()) {
            if (c.is_token() && c.is_identifier())
                names.append(c.token());
        }
    }
    SyntaxElement init;
    if (!supplied) {
        if (i + 4 > declaration.size())
            return not_supported_because(
                declaration, "dcl.struct.bind",
                "a structured binding with no initializer");
        init = declaration[i + 3];
    }
    /* [dcl.pre]/8: "If the decl-specifier-seq contains any decl-specifier
       other than static, thread_local, auto ([dcl.spec.auto]), or
       cv-qualifiers, the program is ill-formed." */
    /* (C++26's constexpr structured bindings, P2686R5: not analysed) */
    if (s.constexpr_)
        return not_supported_because(declaration, "dcl.struct.bind",
                                     "a constexpr structured binding");
    if (s.typedef_ || s.extern_ || s.inline_ || s.virtual_ || s.friend_ ||
        s.explicit_ || s.declares_class ||
        type_table[s.type.unqualified()].kind != type_kind::placeholder ||
        s.type.unqualified() == placeholder_type(true).unqualified())
        return ill_formed(declaration,
                          "the decl-specifier-seq of a structured binding "
                          "declaration shall contain no decl-specifier other "
                          "than static, thread_local, auto, or "
                          "cv-qualifiers",
                          "dcl.pre");
    if (s.gnu_flags || s.aligned || s.section || s.abi_tag)
        return not_supported_because(declaration, "ext.gnu",
                                     "attributes on a structured binding");
    /* /1: "Let ... S consist of the storage-class-specifiers of the
       decl-specifier-seq (if any)": e's storage duration
       ([basic.stc.static]/1) and, at namespace scope, linkage
       ([basic.link]/3.2: internal for static, or "a non-template variable
       of non-volatile const-qualified type", unless extern or inline,
       which a structured binding declaration cannot have) */
    bool automatic = function_state.active && !s.static_;
    if (c_language && !automatic)
        return not_supported_because(
            declaration, "dcl.link",
            "a static or namespace-scope structured binding in C");
    scoped_value<linkage> storage(binding_storage, linkage::none);
    if (!automatic)
        binding_storage = linkage::external;
    if (!automatic &&
        (s.static_ || (s.type.cv() & (cv_const | cv_volatile)) == cv_const))
        binding_storage = linkage::internal;
    /* [basic.scope.pdecl]/10: "The point of declaration of a structured
       binding is immediately after the identifier-list of the structured
       binding declaration." */
    entity_index first_binding = 0;
    for (token_index t : names) {
        uint32_t name = tokens.identifier(t);
        /* a name the scope (or a block's conflicting scope) declares
           already ([basic.scope.declarative]): clang's redefinition */
        entity_index conflicts = entity_table[current_scope].conflicts;
        entity_index prior = entity_table.find(current_scope, name);
        /* [basic.scope.declarative]/4: "they shall all refer to the same
           entity, or all refer to functions and function templates": a
           name the identifier-list has twice (C++26's placeholder _,
           P2169, may be: not analysed) */
        if (prior && first_binding && prior >= first_binding &&
            name != tokens.pp.identifier("_", 1) + 1)
            return ill_formed_at_token(t,
                                       "declarations of the same name in a "
                                       "declarative region shall all refer "
                                       "to the same entity, or all to "
                                       "functions",
                                       "basic.scope.declarative");
        if (prior || (conflicts && entity_table.find(conflicts, name)))
            return not_supported_because(
                declaration, "basic.scope.declarative",
                "a binding name already declared in the scope");
        entity_index b = entity_table.make(entity_kind::structured_binding,
                                           name, current_scope);
        entity_table.bind(current_scope, name, b);
        if (!first_binding)
            first_binding = b;
    }
    uint32_t count = static_cast<uint32_t>(names.size());
    /* anything not analysed: the names are of what analysis did not
       follow */
    auto give_up = [&](analysis_result r) {
        if (r != analysis_result::well_formed)
            for (uint32_t k = 0; k < count; k++) {
                entity_table[first_binding + k].kind = entity_kind::none;
                note_held(first_binding + k);
            }
        return r;
    };
    expression_meaning m;
    bool named = false, direct = false, initializer_list = false;
    if (supplied) {
        m = *supplied;
    } else {
        SyntaxElement clause;
        /* [dcl.pre]/8: "The initializer shall be of the form "=
           assignment-expression", of the form "{ assignment-expression }",
           or of the form "( assignment-expression )"" */
        if (!initializer_form(init, clause, direct, initializer_list))
            return give_up(ill_formed(declaration,
                                      "the initializer of a structured "
                                      "binding declaration shall be of the "
                                      "form = E, { E }, or ( E )",
                                      "dcl.pre"));
        analysis_result r = expression(clause, m);
        if (r != analysis_result::well_formed)
            return give_up(r);
        named = names_entity(clause);
    }
    /* "where the assignment-expression is of array or non-union class
       type" */
    const struct type source = type_table[m.type.unqualified()];
    bool array = source.kind == type_kind::array;
    if (!array) {
        if (source.kind != type_kind::class_type &&
            source.kind != type_kind::fundamental &&
            source.kind != type_kind::pointer &&
            source.kind != type_kind::enumeration)
            return give_up(not_supported_because(
                declaration, "dcl.struct.bind",
                "a structured binding of this kind of type"));
        if (source.kind == type_kind::class_type &&
            entity_table[source.entity].kind != entity_kind::class_type)
            return give_up(not_supported_because(
                declaration, "class.pre",
                "a structured binding of a class that was not analysed"));
        if (source.kind != type_kind::class_type ||
            entity_table[source.entity].union_)
            return give_up(ill_formed(declaration,
                                      "the initializer of a structured "
                                      "binding declaration shall be of array "
                                      "or non-union class type",
                                      "dcl.pre"));
    }
    entity_index e = 0;
    analysis_result r = analysis_result::well_formed;
    if (array && !reference) {
        /* (of static storage duration: the reference the elements are
           copied from would need a symbol) */
        if (!source.bound_known || !automatic)
            return give_up(not_supported_because(
                declaration, "dcl.struct.bind",
                "an array copy of unknown bound or static storage"));
        /* /3 (below), checked before the elements are copied */
        if (source.bound != count)
            return give_up(ill_formed(declaration,
                                      "the number of elements in the "
                                      "identifier-list shall be equal to the "
                                      "number of elements of E",
                                      "dcl.struct.bind"));
        r = array_binding_object(declaration, at, m, s, direct, e);
    } else {
        /* "Otherwise, e is defined as-if by
               attribute-specifier-seq_opt decl-specifier-seq
               ref-qualifier_opt e initializer ;" */
        qualified_type type = s.type;
        if (reference)
            type = reference_to(type, rvalue);
        r = binding_variable(declaration, m, type, named, direct,
                             initializer_list, e);
    }
    if (r != analysis_result::well_formed)
        return give_up(r);
    entity_table[e].first_binding = first_binding;
    entity_table[e].binding_count = count;
    /* "The type of the id-expression e is called E." */
    qualified_type type = entity_table[e].type;
    const struct type &declared = type_table[type.unqualified()];
    if (declared.kind == type_kind::lvalue_reference ||
        declared.kind == type_kind::rvalue_reference)
        type = declared.operand;
    const struct type object = type_table[type.unqualified()];
    for (uint32_t k = 0; k < count; k++) {
        entity_table[first_binding + k].binding_object = e;
        entity_table[first_binding + k].field = k;
    }
    if (object.kind == type_kind::array) {
        /* /3: "If E is an array type with element type T, the number of
           elements in the identifier-list shall be equal to the number of
           elements of E.  Each vi is the name of an lvalue that refers to
           the element i of the array and whose type is T; the referenced
           type is T." */
        if (!object.bound_known || object.bound != count)
            return give_up(ill_formed(declaration,
                                      "the number of elements in the "
                                      "identifier-list shall be equal to the "
                                      "number of elements of E",
                                      "dcl.struct.bind"));
    } else {
        bool tuple = false;
        uint64_t size = 0;
        r = tuple_size_of(declaration, type, tuple, size);
        if (r != analysis_result::well_formed)
            return give_up(r);
        if (tuple) {
            /* (of static storage duration: r_i's symbols) */
            if (!automatic)
                return give_up(not_supported_because(
                    declaration, "dcl.struct.bind",
                    "a tuple-like binding of static storage"));
            if (size != count)
                return give_up(ill_formed(declaration,
                                          "the number of elements in the "
                                          "identifier-list shall be equal "
                                          "to the value of "
                                          "std::tuple_size<E>::value",
                                          "dcl.struct.bind"));
            /* /4: "Given the type Ti designated by std::tuple_element<i,
               E>::type and the type Ui designated by either Ti& or Ti&&,
               where Ui is an lvalue reference if the initializer is an
               lvalue and an rvalue reference otherwise, variables are
               introduced with unique names ri as follows:
                   S Ui ri = initializer ;
               Each vi is the name of an lvalue of type Ti that refers to
               the object bound to ri; the referenced type is Ti." */
            for (uint32_t k = 0; k < count; k++) {
                qualified_type element;
                expression_meaning call;
                r = tuple_element_of(declaration, k, type, element);
                if (r == analysis_result::well_formed)
                    r = binding_get_call(declaration, names[k], k, e, type,
                                         call);
                entity_index v = 0;
                if (r == analysis_result::well_formed)
                    r = binding_variable(
                        declaration, call,
                        reference_to(element,
                                     call.category != value_category::lvalue),
                        false, false, false, v);
                if (r != analysis_result::well_formed)
                    return give_up(r);
                entity_table[first_binding + k].binding_target = v;
                entity_table[first_binding + k].type = element;
            }
            return analysis_result::well_formed;
        }
        /* /5: "Otherwise, ... the number of elements in the
           identifier-list shall be equal to the number of non-static data
           members of E.  Designating the non-static data members of E as
           m0, m1, m2, ... (in declaration order), each vi is the name of an
           lvalue that refers to the member mi of e and whose type is cv Ti,
           where Ti is the declared type of that member; the referenced
           type is cv Ti." */
        entity_index holder = 0;
        r = binding_members_class(declaration, object.entity, holder);
        if (r != analysis_result::well_formed)
            return give_up(r);
        /* named outside the function it is declared in, a member is named
           as e.name in the context of the structured binding declaration
           (/5), not where it is used: only public members of E itself */
        if (!automatic && holder != object.entity)
            return give_up(not_supported_because(
                declaration, "dcl.struct.bind",
                "static bindings of members of a base class"));
        const entity &h = entity_table[holder];
        uint32_t k = 0;
        for (uint32_t j = 0; j < h.member_count; j++) {
            entity_index x = class_members[h.first_member + j];
            if (!binding_data_member(entity_table[x]))
                continue;
            if (!automatic && entity_table[x].access)
                return give_up(not_supported_because(
                    declaration, "dcl.struct.bind",
                    "static bindings of non-public members"));
            if (k < count)
                entity_table[first_binding + k].binding_target = x;
            k++;
        }
        if (k != count)
            return give_up(ill_formed(declaration,
                                      "the number of elements in the "
                                      "identifier-list shall be equal to the "
                                      "number of non-static data members of "
                                      "E",
                                      "dcl.struct.bind"));
    }
    /* each binding's referenced type, the type of what it names (/5: "well-formed
       when named as e.name in the context of the structured binding") */
    for (uint32_t k = 0; k < count; k++) {
        entity_index b = first_binding + k;
        expression_meaning use;
        r = structured_binding_use(names[k], b, use);
        if (r != analysis_result::well_formed)
            return give_up(r);
        entity_index member = entity_table[b].binding_target;
        entity_table[b].type = use.type;
        if (member) {
            type_kind kind =
                type_table[entity_table[member].type.unqualified()].kind;
            if (kind == type_kind::lvalue_reference ||
                kind == type_kind::rvalue_reference)
                entity_table[b].type = entity_table[member].type;
        }
    }
    return analysis_result::well_formed;
}

/*
 * [expr.prim.id.unqual]/2: "The expression is an lvalue if the entity is
 * a ... structured binding": the lvalue the binding b names, element i of
 * e (/3: built-in [expr.sub], *(e + i)), r_i (/4) or e.m_i (/5).
 */
analysis_result semantic_analysis::structured_binding_use(token_index t,
                                                          entity_index b,
                                                          expression_meaning &m)
{
    const entity x = entity_table[b];
    /* /2: "If the initializer refers to one of the names introduced by
       the structured binding declaration, the program is ill-formed." */
    if (!x.binding_object)
        return ill_formed_at_token(t,
                                   "the initializer of a structured binding "
                                   "declaration shall not refer to one of the "
                                   "names it introduces",
                                   "dcl.struct.bind");
    /* named in a lambda-expression (or a local class) of the function it
       is declared in: the capture of a structured binding */
    entity_index s = x.enclosing;
    while (s && entity_table[s].kind == entity_kind::block_scope)
        s = entity_table[s].enclosing;
    if (entity_table[x.binding_object].automatic &&
        (!function_state.active || s != function_state.function)) {
        /* [expr.prim.lambda.capture]/4 (C++20): a structured binding is a
           local entity, captured as a variable of its type; in a local
           class's member function it is not odr-usable
           ([basic.def.odr]/10) */
        if (!function_state.active || !current_lambda())
            return not_supported_because(
                current_declaration, "basic.def.odr",
                "a use of a binding from another function");
        bool deferred = false, plain = false;
        VTRY(captured, captured_entity(t, b, m, deferred, plain));
        if (!plain) {
            m.entity = 0;
            m.binding = b;
            return analysis_result::well_formed;
        }
    }
    analysis_result r = analysis_result::well_formed;
    if (x.binding_target &&
        entity_table[x.binding_target].kind == entity_kind::variable &&
        !entity_table[x.binding_target].nonstatic_member) {
        r = named_entity(t, x.binding_target, m, false);
    } else {
        r = named_entity(t, x.binding_object, m, false);
        if (r == analysis_result::well_formed && x.binding_target) {
            r = member_access(t, x.binding_target, m);
        } else if (r == analysis_result::well_formed) {
            r = array_element(current_declaration, t, m, x.field);
        }
    }
    if (r != analysis_result::well_formed)
        return r;
    m.entity = 0;
    m.binding = b;
    return analysis_result::well_formed;
}

} // namespace cplusplus
