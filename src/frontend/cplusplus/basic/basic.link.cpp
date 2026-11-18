/*
 * basic.link.cpp: [basic.link] Program and linkage: the translation unit.
 *
 *     translation-unit:
 *         declaration-seq_opt
 *         global-module-fragment_opt module-declaration declaration-seq_opt private-module-fragment_opt
 *
 * [basic.link]/1: "A translation unit consists of a sequence of
 * declarations."
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::translation_unit()
{
    size_t mark = begin_node();
    token_index first = position;
    /* the second alternative: a module unit ([module.unit]), from C++20 */
    if (tokens.files.behavior.has(language_feature::modules) &&
        (at_module_declaration() ||
         (at_phase4_keyword(preprocessing_token_kind::module_keyword) &&
          at(preprocessing_op_or_punc::SEMICOLON, 1))))
        module_unit_rest(!at_module_declaration());
    else
        declaration_seq(false);
    tree.root = finish_node(syntax_kind::translation_unit, mark, first);
    pending.resize(mark);
    return tree.root;
}

/* [basic.type.qualifier]/3: "An array type whose elements are
   cv-qualified is also considered to have the same cv-qualifications as its
   elements." */
uint32_t cv_of(const types &table, qualified_type t)
{
    const type &record = table[t.unqualified()];
    if (record.kind == type_kind::array)
        return cv_of(table, record.operand);
    return t.cv();
}

namespace
{

/* [basic.link]/11: "declarations for an array object can specify array
   types that differ by the presence or absence of a major array bound" */
bool differ_in_major_bound(const types &table, qualified_type a,
                           qualified_type b)
{
    const type &x = table[a.unqualified()];
    const type &y = table[b.unqualified()];
    return x.kind == type_kind::array && y.kind == type_kind::array &&
           x.operand == y.operand && x.bound_known != y.bound_known;
}

} // namespace

/* [over.load]/3, [dcl.fct]/4: whether two function types have the same
   parameter-type-list */
bool same_parameters(const types &table, type_index a, type_index b)
{
    const type &x = table[a];
    const type &y = table[b];
    if (x.variadic != y.variadic || x.parameter_count != y.parameter_count)
        return false;
    for (uint32_t i = 0; i < x.parameter_count; i++)
        if (!(table.parameters(a)[i] == table.parameters(b)[i]))
            return false;
    return true;
}

/* [basic.link]/4: a class or enumeration of an unnamed namespace has
   internal linkage, an unnamed one without a typedef name for linkage
   purposes and a local one none ([basic.link]/8): whether t is such a
   type or compounded from one */
bool semantic_analysis::type_without_external_linkage(qualified_type t) const
{
    const struct type &record = type_table[t.unqualified()];
    switch (record.kind) {
    case type_kind::class_type:
    case type_kind::enumeration: {
        const entity &x = entity_table[record.entity];
        if (x.kind != entity_kind::class_type &&
            x.kind != entity_kind::enumeration)
            return false;
        return x.linkage_of_name != linkage::external;
    }
    case type_kind::pointer:
    case type_kind::array:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
        return type_without_external_linkage(record.operand);
    case type_kind::function: {
        const qualified_type *parameters =
            type_table.parameters(t.unqualified());
        for (uint32_t i = 0; i < record.parameter_count; i++)
            if (type_without_external_linkage(parameters[i]))
                return true;
        return type_without_external_linkage(record.operand);
    }
    case type_kind::pointer_to_member:
        return type_without_external_linkage(
                   entity_table[record.entity].type) ||
               type_without_external_linkage(record.operand);
    default:
        return false;
    }
}

/*
 * [dcl.init]/7: "A class type T is const-default-constructible if
 * default-initialization of T would invoke a user-provided constructor of T
 * (not inherited from a base class) or if (7.4) each direct non-variant
 * non-static data member M of T has a default member initializer or, if M
 * is of class type X (or array thereof), X is const-default-constructible,
 * (7.5) if T is a union with at least one non-static data member, exactly
 * one variant member has a default member initializer, (7.6) if T is not a
 * union, for each anonymous union member with at least one non-static data
 * member (if any), exactly one non-static data member has a default member
 * initializer, and (7.7) each potentially constructed base class of T is
 * const-default-constructible."  A union's default member initializers
 * (7.5) and anonymous union members (7.6) are not told apart.
 */
bool semantic_analysis::const_default_constructible(entity_index c) const
{
    const entity &x = entity_table[c];
    for (entity_index k = x.first_constructor; k;
         k = entity_table[k].next_constructor)
        if (!entity_table[k].implicit &&
            !type_table[entity_table[k].type.unqualified()].parameter_count)
            return true;
    if (x.union_ && x.member_count)
        return false;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        /* an unnamed bit-field is not a member ([class.bit]/2) */
        if (m.bit_field && !m.name)
            continue;
        /* (7.4): "each direct non-variant non-static data member
           M of T has a default member initializer or ..." */
        if (m.default_initializer && !x.union_)
            continue;
        qualified_type t = m.type;
        while (type_table[t.unqualified()].kind == type_kind::array)
            t = type_table[t.unqualified()].operand;
        if (type_table[t.unqualified()].kind != type_kind::class_type ||
            !const_default_constructible(type_table[t.unqualified()].entity))
            return false;
    }
    for (uint32_t i = 0; i < x.base_count; i++) {
        const entity &b = entity_table[class_bases[x.first_base + i]];
        if (!const_default_constructible(
                type_table[b.type.unqualified()].entity))
            return false;
    }
    return true;
}

/*
 * A name declared at namespace scope by declaration, or declared again
 * ([basic.scope.namespace]/1: "The declarative region of a
 * namespace-definition is its namespace-body.  Entities declared in a
 * namespace-body are said to be members of the namespace").  definition is whether the declaration is a function-definition;
 * an object declaration is a definition unless it is extern ([basic.def]/2:
 * "it contains the extern specifier or a linkage-specification and neither
 * an initializer nor a function-body"); a typedef declaration is none.
 */
analysis_result semantic_analysis::declare(node_index declaration,
                                           token_index id, entity_kind kind,
                                           qualified_type type,
                                           const specified &s, bool definition,
                                           bool initialized,
                                           entity_index &declared_entity)
{
    declared_entity = 0;
    if (instantiating && kind == entity_kind::function)
        return declare_specialization(declaration, id, type, s,
                                      declared_entity);
    uint32_t name = tokens.identifier(id);
    entity_index scope = current_scope;
    entity_index prior = 0;
    bool qualified = qualified_declarator;
    if (qualified) {
        /* a typedef-name is no member a qualified declarator-id declares
           again ([dcl.meaning]/1 has a function or variable in mind):
           clang's error, by a rule C++20 does not word */
        if (kind == entity_kind::typedef_name)
            return not_supported(declaration, "dcl.meaning");
        scope = qualified_scope;
        /* [namespace.memdef]/2: "Members of a named namespace can also be
           defined outside that namespace by explicit qualification of the
           name being defined, provided that the entity being defined was
           already declared in the namespace and the definition appears
           after the point of declaration in a namespace that encloses the
           declaration's namespace." */
        if (!namespace_encloses(current_scope, scope))
            return ill_formed_at_token(id,
                                       "a member of a namespace defined "
                                       "outside it shall be defined in a "
                                       "namespace that encloses the "
                                       "declaration's namespace",
                                       "namespace.memdef");
        /* in its own namespace: clang's extra qualification, an error or a
           warning by the kind of member */
        if (current_scope == scope)
            return not_supported(declaration, "dcl.meaning");
        /* [dcl.meaning]/1: "the declaration shall refer to a previously
           declared member of the class or namespace to which the qualifier
           refers (or, in the case of a namespace, of an element of the
           inline namespace set of that namespace) ... the member shall not
           merely have been introduced by a using-declaration in the scope
           of the class or namespace nominated by the nested-name-specifier
           of the declarator-id" */
        lookup_result found =
            lookup_in_namespace(scope, name, lookup_filter::any);
        if (found.ambiguous || found.unknown || !found.entity)
            /* one not found may be a friend's, declared in a class
               ([namespace.memdef]/3) */
            return not_supported(declaration, "dcl.meaning");
        prior = found.entity;
        entity_index in = entity_table[prior].enclosing;
        if (entity_table.find(found.found_in, name) != prior ||
            found.found_in != in)
            /* named by a using-declaration there, or found through a
               using-directive */
            return not_supported(declaration, "dcl.meaning");
        for (entity_index y = in; y != scope; y = entity_table[y].enclosing)
            if (!y || !entity_table[y].inline_)
                return not_supported(declaration, "dcl.meaning");
        scope = in;
    } else {
        prior = entity_table.find(scope, name);
        /* [basic.link]/7: a member of the namespace declared in a block before,
           whose name was not bound: this declaration is of that entity, and
           binds the name */
        if (!prior && is_namespace(scope) &&
            (kind == entity_kind::function || kind == entity_kind::variable))
            for (entity_index e : block_declared)
                if (entity_table[e].enclosing == scope &&
                    entity_table[e].name == name &&
                    (kind != entity_kind::function ||
                     entity_table[e].kind != entity_kind::function ||
                     same_parameters(type_table,
                                     entity_table[e].type.unqualified(),
                                     type.unqualified()))) {
                    /* [dcl.array]/8: a bound given in the block is not
                       this scope's */
                    if (!(entity_table[e].type == type) &&
                        differ_in_major_bound(type_table, entity_table[e].type,
                                              type))
                        return not_supported(declaration, "dcl.array");
                    prior = e;
                    entity_table.bind(scope, name, e);
                }
        /* [dcl.link]/7: "Two declarations for a function with C language
           linkage with the same function name (ignoring the namespace names
           that qualify it) that appear in different namespace scopes refer
           to the same function.  Two declarations for a variable with C
           language linkage with the same name (ignoring the namespace names
           that qualify it) that appear in different namespace scopes refer
           to the same variable." */
        if (!prior && c_language && is_namespace(scope) &&
            (kind == entity_kind::function || kind == entity_kind::variable))
            for (entity_index e = 1; e < entity_table.size(); e++) {
                const entity &x = entity_table[e];
                if (!x.c_language_linkage || x.kind != kind || x.name != name ||
                    !is_namespace(x.enclosing))
                    continue;
                if (!(x.type == type))
                    return not_supported(declaration, "dcl.link");
                prior = e;
                entity_table.bind(scope, name, e);
                break;
            }
    }
    /* [dcl.typedef]/3: "In a given non-class scope, a typedef specifier
       can be used to redeclare the name of any type declared in that scope
       to refer to the type to which it already refers": typedef struct s
       { ... } s; the name still finds the class or enumeration */
    if (prior && kind == entity_kind::typedef_name &&
        (entity_table[prior].kind == entity_kind::class_type ||
         entity_table[prior].kind == entity_kind::enumeration) &&
        type == entity_table[prior].type) {
        declared_entity = prior;
        return analysis_result::well_formed;
    }
    /* a name a using-declaration brought into the scope ([namespace.udecl]/12:
       "Since a using-declaration is a declaration, the restrictions on
       declarations of the same name in the same declarative region also apply
       to using-declarations"): a function beside functions is an overload, the
       rest conflicts or hides */
    if (prior && !qualified && entity_table.bound_by_using(scope, name))
        return not_supported(declaration, "namespace.udecl");
    /* [basic.scope.hiding]/2: "If a class name or enumeration name and a
       variable, data member, function, or enumerator are declared in the same
       declarative region (in any order) with the same name ..., the class or
       enumeration name is hidden wherever the variable, data member, function,
       or enumerator name is visible." */
    entity_index hides = 0;
    if (prior && (entity_table[prior].kind == entity_kind::class_type ||
                  entity_table[prior].kind == entity_kind::enumeration)) {
        if (kind == entity_kind::typedef_name || qualified ||
            entity_table[prior].enclosing != scope)
            /* a typedef-name for another type: [dcl.typedef]/6 */
            return not_supported(declaration, "basic.scope.hiding");
        hides = prior;
        prior = 0;
    }
    declaration_hides = hides;
    bool block = !is_namespace(scope);
    if (block) {
        /* [dcl.inline]/4: "The inline specifier shall not appear on a block
           scope declaration" */
        if (s.inline_)
            return ill_formed_at_token(id,
                                       "the inline specifier shall not "
                                       "appear on a block scope declaration",
                                       "dcl.inline");
        /* a block-scope declaration of a function, or of a variable with
           extern: it declares a member of the namespace ([basic.link]/6) */
        if (kind == entity_kind::function ||
            (s.extern_ && kind == entity_kind::variable)) {
            if (hides)
                return not_supported(declaration, "basic.scope.hiding");
            return declare_in_block_with_linkage(declaration, id, kind, type, s,
                                                 initialized, declared_entity);
        }
        if (s.extern_)
            return not_supported(declaration, "basic.link");
    }
    /* [dcl.array]/8: "if there is a preceding declaration of the entity in
       the same scope in which the bound was specified, an omitted array
       bound is taken to be the same as in that earlier declaration" */
    if (prior && kind == entity_kind::variable &&
        entity_table[prior].kind == kind &&
        type_table[type.unqualified()].kind == type_kind::array &&
        !type_table[type.unqualified()].bound_known &&
        differ_in_major_bound(type_table, entity_table[prior].type, type))
        type = entity_table[prior].type;
    const struct type &record = type_table[type.unqualified()];
    bool is_reference = record.kind == type_kind::lvalue_reference ||
                        record.kind == type_kind::rvalue_reference;
    if (kind == entity_kind::variable) {
        /* [basic.def]/2: a declaration is a definition unless "it
           contains the extern specifier ... and neither an initializer nor
           a function-body" */
        definition = !s.extern_ || initialized;
        /* [dcl.link]/7: "A declaration directly contained in a
           linkage-specification is treated as if it contains the extern
           specifier ([dcl.stc]) for the purpose of determining the linkage
           of the declared name and whether it is a definition." */
        if (linkage_without_braces && !block)
            definition = initialized;
        /* [dcl.ref]/5: "The declaration of a reference shall contain an
           initializer except when the declaration contains an explicit
           extern specifier" */
        if (is_reference && !s.extern_ && !initialized)
            return ill_formed_at_token(id,
                                       "the declaration of a reference shall "
                                       "contain an initializer except when "
                                       "the declaration contains an explicit "
                                       "extern specifier",
                                       "dcl.ref");
        bool is_void = record.kind == type_kind::fundamental &&
                       record.fundamental == fundamental_type::void_;
        /* [basic.def]/5: "In the definition of an object, the type of that
           object shall not be an incomplete type" */
        if (definition && !initialized &&
            (is_void ||
             (record.kind == type_kind::array && !record.bound_known)))
            return ill_formed_at_token(id,
                                       "in the definition of an object, the "
                                       "type of that object shall not be an "
                                       "incomplete type",
                                       "basic.def");
        /* an extern object of type cv void: clang's error, a rule not found
           yet */
        if (is_void)
            return not_supported(declaration, "basic.types");
        /* [basic.def]/5: "In the definition of an object, the type of that
           object shall not be ... an abstract class type, or a (possibly
           multi-dimensional) array thereof" (a declaration that is no
           definition: clang's error too, from no rule found yet) */
        if (abstract_class(type)) {
            if (!definition)
                return not_supported(declaration, "class.abstract");
            return ill_formed_at_token(id,
                                       "in the definition of an object, the "
                                       "type of that object shall not be an "
                                       "abstract class type",
                                       "basic.def");
        }
        if (record.kind == type_kind::class_type) {
            const entity &c = entity_table[record.entity];
            if (c.kind != entity_kind::class_type)
                /* a class whose definition was not analysed */
                return not_supported(declaration, "class.pre");
            /* [basic.def]/5 */
            if (definition && !c.size)
                return ill_formed_at_token(id,
                                           "in the definition of an object, "
                                           "the type of that object shall "
                                           "not be an incomplete type",
                                           "basic.def");
        }
        /* [dcl.init]/7: "If a program calls for the default-initialization
           of an object of a const-qualified type T, T shall be a
           const-default-constructible class type or array thereof." */
        bool user_default = false;
        qualified_type element = type;
        while (type_table[element.unqualified()].kind == type_kind::array)
            element = type_table[element.unqualified()].operand;
        if (type_table[element.unqualified()].kind == type_kind::class_type)
            user_default = const_default_constructible(
                type_table[element.unqualified()].entity);
        if (definition && !initialized && !user_default &&
            (cv_of(type_table, type) & cv_const))
            return ill_formed_at_token(id,
                                       "the default-initialization of an "
                                       "object of a const-qualified type "
                                       "needs a const-default-constructible "
                                       "class type",
                                       "dcl.init");
    }
    if (kind == entity_kind::typedef_name && definition)
        return not_supported(declaration, "dcl.fct.def.general");
    /* [dcl.typedef]/9: "If the typedef declaration defines an unnamed
       class (or enumeration), the first typedef-name declared by the
       declaration to be that class type (or enumeration type) is used to
       denote the class type (or enumeration type) for linkage purposes
       only" */
    if (kind == entity_kind::typedef_name && !type.cv() &&
        (record.kind == type_kind::class_type ||
         record.kind == type_kind::enumeration)) {
        entity &c = entity_table[record.entity];
        if (!c.name && !c.linkage_name) {
            c.linkage_name = name;
            c.linkage_of_name = linkage::external;
        }
    }
    if (kind != entity_kind::function && s.inline_ &&
        kind != entity_kind::variable)
        return not_supported(declaration, "dcl.inline");
    /* [basic.link]/3: "A name having namespace scope has internal linkage
       if it is the name of a variable, variable template, function, or
       function template that is explicitly declared static; or a
       non-template variable of non-volatile const-qualified type, unless it
       is explicitly declared extern, or it is inline or exported, or it was
       previously declared and the prior declaration did not have internal
       linkage" */
    linkage implied = linkage::external;
    if (kind == entity_kind::typedef_name)
        implied = linkage::none;
    else if (s.static_)
        implied = linkage::internal;
    uint32_t cv = cv_of(type_table, type);
    bool const_object = kind == entity_kind::variable && (cv & cv_const) &&
                        !(cv & cv_volatile) && !s.extern_ && !s.inline_;
    if (block)
        return declare_local(declaration, id, kind, type, s, declared_entity);
    /* [basic.link]/4: "An unnamed namespace or a namespace declared
       directly or indirectly within an unnamed namespace has internal
       linkage. ... A name having namespace scope that has not been given
       internal linkage above and that is the name of a variable; or a
       function; ... has its linkage determined as follows: if the enclosing
       namespace has internal linkage, the name has internal linkage" */
    bool in_unnamed = within_unnamed_namespace(scope);
    if (in_unnamed && implied == linkage::external)
        implied = linkage::internal;
    /* [over.pre]/1: "When two or more different declarations are specified
       for a single name in the same scope, that name is said to be
       overloaded"; [over.load]/1: "Not all function declarations can be
       overloaded."  A function of the name with the same parameter-type-list
       is the one this declaration redeclares; with none, it declares another
       function of the set */
    entity_index overloads = 0;
    if (prior && kind == entity_kind::function &&
        entity_table[prior].kind == entity_kind::function) {
        entity_index same = 0;
        for (entity_index o = prior; o && !same;
             o = entity_table[o].next_overload)
            if (same_parameters(type_table, entity_table[o].type.unqualified(),
                                type.unqualified()))
                same = o;
        /* a function declared before by a friend declaration only
           ([namespace.memdef]/3), whose name is bound to others */
        if (!same && !qualified && declared_unbound(scope, name, type))
            return not_supported(declaration, "class.friend");
        if (!same && qualified)
            /* [dcl.meaning]/1: "the declaration shall refer to a previously
               declared member of the class or namespace to which the
               qualifier refers" */
            return ill_formed_at_token(id,
                                       "a qualified declaration shall refer "
                                       "to a previously declared member of "
                                       "the namespace",
                                       "dcl.meaning");
        if (!same) {
            /* [dcl.link]/6: "At most one function with a particular name
               can have C language linkage."  /1: "All function types,
               function names with external linkage, and variable names with
               external linkage have a language linkage": a function with
               internal linkage has none */
            for (entity_index o = prior; o; o = entity_table[o].next_overload)
                if (c_language && !s.static_ && !in_unnamed &&
                    entity_table[o].c_language_linkage &&
                    entity_table[o].linkage_of_name != linkage::internal)
                    return ill_formed_at_token(id,
                                               "at most one function with a "
                                               "particular name can have C "
                                               "language linkage",
                                               "dcl.link");
            overloads = prior;
        }
        prior = same;
    }
    if (prior) {
        entity &e = entity_table[prior];
        /* declared by a declaration not analysed (hold_names) */
        if (e.kind == entity_kind::none)
            return not_supported(declaration, "basic.scope.declarative");
        /* [basic.scope.declarative]/4: "Given a set of declarations in a
           single declarative region, each of which specifies the same
           unqualified name, they shall all refer to the same entity, or all
           refer to functions and function templates" */
        if (e.kind != kind)
            return ill_formed_at_token(id,
                                       "declarations of the same name in a "
                                       "declarative region shall all refer to "
                                       "the same entity, or all to functions",
                                       "basic.scope.declarative");
        if (kind == entity_kind::typedef_name) {
            /* [dcl.typedef]/6: "In a given scope, a typedef specifier shall
               not be used to redeclare the name of any type declared in
               that scope to refer to a different type." */
            if (!(e.type == type))
                return ill_formed_at_token(id,
                                           "a typedef specifier shall not be "
                                           "used to redeclare the name of a "
                                           "type to refer to a different type",
                                           "dcl.typedef");
        } else if (kind == entity_kind::function) {
            /* (a function of the same parameters: overloads above) */
            /* [over.load]/2: "Function declarations that differ only in the
               return type, the exception specification, or both cannot be
               overloaded." */
            if (!(e.type == type))
                return ill_formed_at_token(id,
                                           "function declarations that differ "
                                           "only in the return type cannot be "
                                           "overloaded",
                                           "over.load");
        } else if (!(e.type == type) &&
                   !differ_in_major_bound(type_table, e.type, type)) {
            /* [basic.link]/11: "the types specified by all declarations
               referring to a given variable or function shall be
               identical" */
            return ill_formed_at_token(id,
                                       "the types specified by all "
                                       "declarations referring to a given "
                                       "variable shall be identical",
                                       "basic.link");
        }
        /* [basic.def.odr]/1: "No translation unit shall contain more than
           one definition of any variable, function" */
        if (definition && e.defined)
            return ill_formed_at_token(id,
                                       "no translation unit shall contain "
                                       "more than one definition of any "
                                       "variable or function",
                                       "basic.def.odr");
        /* [dcl.constexpr]/1: "If any declaration of a function or function
           template has a constexpr or consteval specifier, then all its
           declarations shall contain the same specifier." */
        if (kind == entity_kind::function && e.kind == entity_kind::function &&
            e.constexpr_ != s.constexpr_)
            return ill_formed_at_token(id,
                                       "if any declaration of a function has "
                                       "a constexpr specifier, then all its "
                                       "declarations shall contain the same "
                                       "specifier",
                                       "dcl.constexpr");
        /* [dcl.stc]/6: "The linkages implied by successive declarations for
           a given entity shall agree." */
        if (implied == linkage::internal && !in_unnamed &&
            e.linkage_of_name == linkage::external &&
            tokens.files.behavior.microsoft_extensions) {
            /* Microsoft's extension: the name keeps its external linkage */
            analysis_result r = extension_used(
                diagnostic_id::sema_static_after_non_static, id, nullptr);
            if (r != analysis_result::well_formed)
                return r;
        } else if (implied == linkage::internal && !in_unnamed &&
                   e.linkage_of_name == linkage::external)
            return ill_formed_at_token(id,
                                       "the linkages implied by successive "
                                       "declarations for a given entity "
                                       "shall agree",
                                       "dcl.stc");
        if (definition)
            e.defined = true;
        /* the bound a later declaration gives an array */
        if (type_table[type.unqualified()].kind == type_kind::array &&
            type_table[type.unqualified()].bound_known)
            e.type = type;
        entity_table.add_declaration(prior, declaration);
        declared_entity = prior;
    } else {
        if (const_object)
            implied = linkage::internal;
        /* [basic.link]/4.3: an unnamed class without a typedef name for
           linkage purposes ([dcl.typedef]/9) has no name for linkage; a
           variable of its type is [basic.link]/8's "entity with no
           linkage" type's, which clang gives internal linkage */
        qualified_type object = type;
        while (type_table[object.unqualified()].kind == type_kind::array)
            object = type_table[object.unqualified()].operand;
        bool unnamed_type = false;
        if (kind == entity_kind::variable && implied == linkage::external &&
            type_table[object.unqualified()].kind == type_kind::class_type &&
            entity_table[type_table[object.unqualified()].entity].kind ==
                entity_kind::class_type &&
            !entity_table[type_table[object.unqualified()].entity].name &&
            !entity_table[type_table[object.unqualified()].entity]
                 .linkage_name) {
            implied = linkage::internal;
            unnamed_type = true;
        }
        /* [impl.clang] a function or variable whose type is made of a
           class or enumeration of an unnamed namespace cannot be named in
           another translation unit: its symbol is local */
        if ((kind == entity_kind::variable || kind == entity_kind::function) &&
            implied == linkage::external && !c_language &&
            type_without_external_linkage(type)) {
            implied = linkage::internal;
            unnamed_type = true;
        }
        if (!overloads)
            no_claims_after_hiding(name);
        if (overloads && entity_table[overloads].named_by_using)
            /* [namespace.udecl]/11: a using-declaration names the
               declarations before it, so this overload is not one of the
               set it introduced; the name is bound to the chain, not to a
               copy of it */
            return not_supported(declaration, "namespace.udecl");
        entity_index e = entity_table.make(kind, name, scope);
        entity_table[e].hidden_type = hides;
        entity_table[e].unnamed_type_linkage = unnamed_type;
        entity_table[e].type = type;
        entity_table[e].linkage_of_name = implied;
        entity_table[e].defined = definition;
        entity_table.add_declaration(e, declaration);
        if (overloads) {
            /* another function of the set: last in its chain, the name
               still bound to the first */
            entity_index last = overloads;
            while (entity_table[last].next_overload)
                last = entity_table[last].next_overload;
            entity_table[last].next_overload = e;
            entity_table[e].hidden_type = entity_table[overloads].hidden_type;
        } else {
            entity_table.bind(scope, name, e);
        }
        declared_entity = e;
    }
    /* [basic.def.odr]/11: "A definition of an inline function or variable
       shall be reachable from the end of every definition domain in which
       it is odr-used outside of a discarded statement."  So it is emitted
       where it is used; an entity is inline once a declaration says so */
    if (s.inline_)
        entity_table[declared_entity].inline_ = true;
    /* [dcl.constexpr]/1: "A function or static data member declared with
       the constexpr or consteval specifier is implicitly an inline function
       or variable"; the rules on a definition's body are function_body's
       (dcl.fct.def.general.cpp) */
    if (s.constexpr_ &&
        entity_table[declared_entity].kind == entity_kind::function) {
        entity_table[declared_entity].inline_ = true;
        entity_table[declared_entity].constexpr_ = true;
    }
    declared_name d;
    d.declaration = declaration;
    d.outermost = current_declaration;
    d.entity = declared_entity;
    d.id = id;
    d.kind = kind;
    d.type = type;
    /* a qualified name's claim would be looked up where the declaration
       is, not in its namespace */
    d.claimable = declarations_claimable && !qualified;
    declared.append(d);
    return analysis_result::well_formed;
}

/*
 * [basic.link]/6: "The name of a function declared in block scope and the name
 * of a variable declared by a block scope extern declaration have linkage. ...
 * If there is a visible declaration of an entity with linkage, ignoring
 * entities declared outside the innermost enclosing namespace scope, such that
 * the block scope declaration would be a (possibly ill-formed) redeclaration if
 * the two declarations appeared in the same declarative region, the block scope
 * declaration declares that same entity and receives the linkage of the
 * previous declaration.  If there is more than one such matching entity, the
 * program is ill-formed. Otherwise, if no matching entity is found, the block
 * scope entity receives external linkage."  /7: "When a block scope declaration
 * of an entity with linkage is not found to refer to some other declaration,
 * then that entity is a member of the innermost enclosing namespace. However
 * such a declaration does not introduce the member name in its namespace
 * scope."
 *
 * The name is bound in the block to the namespace's entity; an entity
 * first declared so is kept (block_declared) for the declarations of it
 * that follow, in a block or in its namespace.
 */
analysis_result semantic_analysis::declare_in_block_with_linkage(
    node_index declaration, token_index id, entity_kind kind,
    qualified_type type, const specified &s, bool initialized,
    entity_index &declared_entity)
{
    uint32_t name = tokens.identifier(id);
    entity_index block = current_scope;
    entity_index space = enclosing_namespace(block);
    /* [dcl.stc]/4: "There can be no static function declarations within a
       block" */
    if (kind == entity_kind::function && s.static_)
        return ill_formed_at_token(id,
                                   "there can be no static function "
                                   "declarations within a block",
                                   "dcl.stc");
    /* [dcl.init]/5: "A declaration of a block-scope variable with external
       or internal linkage that has an initializer is ill-formed." */
    if (initialized)
        return ill_formed_at_token(id,
                                   "a declaration of a block-scope variable "
                                   "with external or internal linkage that "
                                   "has an initializer is ill-formed",
                                   "dcl.init");
    if (s.constexpr_ || function_cv)
        return not_supported(declaration, "basic.link");
    if (kind == entity_kind::variable) {
        const struct type &record = type_table[type.unqualified()];
        if (record.kind == type_kind::class_type &&
            entity_table[record.entity].kind != entity_kind::class_type)
            return not_supported(declaration, "class.pre");
        if (record.kind == type_kind::fundamental &&
            record.fundamental == fundamental_type::void_)
            return not_supported(declaration, "basic.types");
    }
    /* whether the entity e is what this declaration would redeclare */
    auto matches = [&](entity_index e, analysis_result &r) {
        const entity &x = entity_table[e];
        r = analysis_result::well_formed;
        if (x.kind != kind) {
            /* [basic.scope.declarative]/4: "they shall all refer to the
               same entity, or all refer to functions and function
               templates" */
            r = ill_formed_at_token(id,
                                    "declarations of the same name in a "
                                    "declarative region shall all refer to "
                                    "the same entity, or all to functions",
                                    "basic.scope.declarative");
            return false;
        }
        if (kind == entity_kind::function) {
            if (!same_parameters(type_table, x.type.unqualified(),
                                 type.unqualified())) {
                /* another function of the name: an overload */
                r = not_supported(declaration, "over");
                return false;
            }
            /* [over.load]/2 */
            if (!(x.type == type)) {
                r = ill_formed_at_token(id,
                                        "function declarations that differ "
                                        "only in the return type cannot be "
                                        "overloaded",
                                        "over.load");
                return false;
            }
            return true;
        }
        /* [basic.link]/11: "the types specified by all declarations
           referring to a given variable or function shall be identical,
           except that declarations for an array object can specify array
           types that differ by the presence or absence of a major array
           bound" */
        if (!(x.type == type) &&
            !differ_in_major_bound(type_table, x.type, type)) {
            r = ill_formed_at_token(id,
                                    "the types specified by all "
                                    "declarations referring to a given "
                                    "variable shall be identical",
                                    "basic.link");
            return false;
        }
        /* [dcl.array]/8: "if there is a preceding declaration of the
           entity in the same scope in which the bound was specified, an
           omitted array bound is taken to be the same as in that earlier
           declaration": a bound this block's declaration gives or omits is
           the type the name has in this scope only, which an entity's one
           type does not say */
        if (!(x.type == type)) {
            r = not_supported(declaration, "dcl.array");
            return false;
        }
        return true;
    };
    entity_index target = 0;
    entity_index here = entity_table.find(block, name);
    if (here) {
        const entity &x = entity_table[here];
        if (x.kind == entity_kind::none ||
            entity_table.bound_by_using(block, name))
            return not_supported(declaration, "basic.scope.declarative");
        /* a local entity of the block: [basic.scope.declarative]/4 */
        if (!is_namespace(x.enclosing))
            return ill_formed_at_token(id,
                                       "declarations of the same name in a "
                                       "declarative region shall all refer "
                                       "to the same entity, or all to "
                                       "functions",
                                       "basic.scope.declarative");
    }
    lookup_result visible = lookup_unqualified(name, lookup_filter::any);
    if (visible.unknown || visible.ambiguous)
        return not_supported(declaration, "basic.link");
    if (visible.entity) {
        const entity &x = entity_table[visible.entity];
        bool has_linkage = (x.kind == entity_kind::function ||
                            x.kind == entity_kind::variable) &&
                           is_namespace(x.enclosing);
        if (has_linkage && x.enclosing == space) {
            /* found there, or through a block's declaration of it; not
               through a using-declaration or using-directive */
            if (entity_table.bound_by_using(visible.found_in, name) ||
                (is_namespace(visible.found_in) && visible.found_in != space))
                return not_supported(declaration, "basic.link");
            analysis_result r;
            if (!matches(visible.entity, r))
                return r;
            target = visible.entity;
        } else if (!is_namespace(visible.found_in) ||
                   visible.found_in == space) {
            /* a local name that hides the namespace's, a using-declaration
               in a block, or a name of the namespace that is no function
               or variable: what the declaration then declares has no
               rules yet */
            return not_supported(declaration, "basic.link");
        }
        /* else: an entity of an outer namespace, which is ignored */
    }
    if (!target) {
        /* the name is not visible: one a block declared before, or (a
           name hidden by nothing) one of the namespace */
        for (entity_index e : block_declared)
            if (entity_table[e].enclosing == space &&
                entity_table[e].name == name)
                target = e;
        entity_index member = entity_table.find(space, name);
        if (!target && member)
            /* hidden by an outer namespace's name found first? not so:
               lookup would have found it */
            return not_supported(declaration, "basic.link");
        if (target) {
            analysis_result r;
            if (!matches(target, r))
                return r;
        }
    }
    if (!target) {
        target = entity_table.make(kind, name, space);
        entity &made = entity_table[target];
        made.type = type;
        made.linkage_of_name = linkage::external;
        /* [basic.link]/4: within an unnamed namespace, internal */
        if (within_unnamed_namespace(space))
            made.linkage_of_name = linkage::internal;
        /* [dcl.link]/5 */
        made.c_language_linkage = c_language;
        block_declared.append(target);
    }
    if (s.inline_)
        return ill_formed_at_token(id,
                                   "the inline specifier shall not appear on "
                                   "a block scope declaration",
                                   "dcl.inline");
    entity_table.add_declaration(target, declaration);
    entity_table.bind(block, name, target);
    declared_entity = target;
    declared_name d;
    d.declaration = declaration;
    d.outermost = current_declaration;
    d.entity = target;
    d.id = id;
    d.kind = kind;
    d.type = type;
    d.claimable = declarations_claimable;
    declared.append(d);
    return analysis_result::well_formed;
}

void semantic_analysis::no_claims_after_hiding(uint32_t name)
{
    lookup_result outer = lookup_unqualified(name, lookup_filter::any);
    if (!outer.entity && !outer.unknown && !outer.ambiguous)
        return;
    /* the expressions of the declaration (in a block: of the statement)
       analysed so far */
    for (size_t i = expressions.size(); i-- > 0;) {
        analysed_expression &x = expressions[i];
        if (x.declaration != current_declaration || x.place != claim_place)
            break;
        x.placeable = false;
    }
}

/*
 * [basic.scope.block]/1: "A name declared in a block is local to that
 * block; it has block scope."  A local variable or typedef-name, its
 * object's rules checked by declare.
 */
analysis_result semantic_analysis::declare_local(
    node_index declaration, token_index id, entity_kind kind,
    qualified_type type, const specified &s, entity_index &declared_entity)
{
    uint32_t name = tokens.identifier(id);
    entity_index scope = current_scope;
    entity_index prior = entity_table.find(scope, name);
    /* the class or enumeration of the block the name hides (declare) */
    entity_index hides = declaration_hides;
    if (hides)
        prior = 0;
    if (prior) {
        const entity &e = entity_table[prior];
        if (e.kind == entity_kind::none)
            return not_supported(declaration, "basic.scope.declarative");
        /* C++26's name-independent declarations (P2169, not in C++20): a
           variable named _ may be declared again */
        owned_buffer spelling;
        tokens.spelling(id, spelling.get());
        if (spelling.size() == 1 && spelling.data()[0] == '_')
            return not_supported(declaration, "basic.scope.declarative");
        /* [dcl.typedef]/6: "In a given non-class scope, a typedef
           specifier can be used to redeclare the name of any type declared
           in that scope to refer to the type to which it already refers." */
        if (e.kind == entity_kind::typedef_name &&
            kind == entity_kind::typedef_name) {
            if (!(e.type == type))
                return ill_formed_at_token(id,
                                           "a typedef specifier shall not be "
                                           "used to redeclare the name of a "
                                           "type to refer to a different type",
                                           "dcl.typedef");
            entity_table.add_declaration(prior, declaration);
            declared_entity = prior;
            return analysis_result::well_formed;
        }
        if (e.kind != kind)
            return ill_formed_at_token(id,
                                       "declarations of the same name in a "
                                       "declarative region shall all refer to "
                                       "the same entity, or all to functions",
                                       "basic.scope.declarative");
        /* [basic.def.odr]/1: "No translation unit shall contain more than
           one definition of any variable" */
        return ill_formed_at_token(id,
                                   "no translation unit shall contain more "
                                   "than one definition of any variable",
                                   "basic.def.odr");
    }
    entity_index conflicts = entity_table[scope].conflicts;
    if (conflicts) {
        entity_index other = entity_table.find(conflicts, name);
        if (other && entity_table[other].kind == entity_kind::none)
            return not_supported(declaration, "basic.scope.block");
        if (other && entity_table[conflicts].parameter_scope)
            /* [basic.scope.param]/1: "A parameter name shall not be
               redeclared in the outermost block of the function
               definition" */
            return ill_formed_at_token(id,
                                       "a parameter name shall not be "
                                       "redeclared in the outermost block of "
                                       "the function definition",
                                       "basic.scope.param");
        if (other)
            /* [basic.scope.block]/3: "Names declared in the init-statement,
               ... and in the condition of if, while, for, and switch
               statements are local to the if, while, for, or switch
               statement (including the controlled statement), and shall not
               be redeclared in a subsequent condition of that statement nor
               in the outermost block (or, for the if statement, any of the
               outermost blocks) of the controlled statement." */
            return ill_formed_at_token(id,
                                       "a name declared in the init-statement "
                                       "or condition of a statement shall not "
                                       "be redeclared in the outermost block "
                                       "of the controlled statement",
                                       "basic.scope.block");
    }
    no_claims_after_hiding(name);
    entity_index e = entity_table.make(kind, name, scope);
    entity_table[e].hidden_type = hides;
    entity_table[e].type = type;
    entity_table[e].linkage_of_name = linkage::none;
    entity_table[e].defined = kind == entity_kind::variable;
    /* [basic.stc.auto]/1: "Variables that belong to a block or parameter
       scope and are not explicitly declared static, thread_local, or
       extern have automatic storage duration." */
    entity_table[e].automatic = kind == entity_kind::variable && !s.static_;
    entity_table.add_declaration(e, declaration);
    entity_table.bind(scope, name, e);
    declared_entity = e;
    declared_name d;
    d.declaration = declaration;
    d.outermost = current_declaration;
    d.entity = declared_entity;
    d.id = id;
    d.kind = kind;
    d.type = type;
    d.claimable = declarations_claimable;
    declared.append(d);
    return analysis_result::well_formed;
}

entity_index semantic_analysis::open_scope(entity_index conflicts,
                                           bool parameters)
{
    entity_index s =
        entity_table.make(entity_kind::block_scope, 0, current_scope);
    entity_table[s].conflicts = conflicts;
    entity_table[s].parameter_scope = parameters;
    current_scope = s;
    return s;
}

} // namespace cplusplus
