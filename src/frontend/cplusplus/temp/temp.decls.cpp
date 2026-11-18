/*
 * temp.decls.cpp: the semantic side of [temp.pre], [temp.expl.spec],
 * [temp.explicit] and [temp.deduct.guide]: the declarations of templates
 * and of their specializations.  (The grammar is the parser's, in
 * temp.pre.cpp and temp.concept.cpp.)
 *
 * The skeleton of S6b: each declaration answers the subclause whose rules
 * it waits for, by what it declares, so that cxx-sema.py's list of what is
 * unsupported says which kind of template blocks most.  hold_names (called
 * for any declaration that is not well-formed) keeps the names they
 * declare, marked as a template's ([temp.names]).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [temp.pre]/1: "A template defines a family of classes, functions, or
 * variables, an alias for a family of types, or a concept."
 */
analysis_result semantic_analysis::template_declaration(node_index n)
{
    const syntax_node &node = tree[n];
    if (!node.child_count || tree.children(n)[node.child_count - 1].is_token())
        return not_supported(n, "temp.pre");
    node_index inner = tree.children(n)[node.child_count - 1].index();
    switch (tree[inner].kind) {
    /* [temp.mem]/1: "A member template can be defined within or outside
       its class definition or class template definition" (template<...>
       template<...> ...) */
    case syntax_kind::template_declaration:
        return not_supported(n, "temp.mem");
    /* [temp.concept]/1: "A concept is a template that defines constraints
       on its template arguments." */
    case syntax_kind::concept_definition:
        return not_supported(n, "temp.concept");
    /* [temp.alias]/1: "A template-declaration in which the declaration is
       an alias-declaration declares the identifier to be an alias
       template." */
    case syntax_kind::alias_declaration:
        return not_supported(n, "temp.alias");
    /* [temp.fct]/1: "A function template defines an unbounded set of
       related functions." */
    case syntax_kind::function_definition:
        /* [temp.mem.func]/1: "A member function of a class template may be
           defined outside of the class template definition in which it is
           declared." */
        if (out_of_class_member(inner))
            return member_definition_of_template(n, inner);
        return function_template_definition(n, inner);
    case syntax_kind::simple_declaration: {
        /* [temp.class]/1: "A class template defines the layout and
           operations for an unbounded set of related types." (a
           declaration with no declarator: a class's) */
        bool declarators = false;
        for (uint32_t i = 0; i < tree[inner].child_count; i++)
            if (syntax_is(tree, tree.children(inner)[i],
                          syntax_kind::init_declarator_list))
                declarators = true;
        if (!declarators) {
            const syntax_node &d = tree[inner];
            if (d.child_count == 2 &&
                syntax_is(tree, tree.children(inner)[0],
                          syntax_kind::decl_specifier_seq)) {
                node_index seq = tree.children(inner)[0].index();
                if (tree[seq].child_count == 1 &&
                    syntax_is(tree, tree.children(seq)[0],
                              syntax_kind::class_specifier))
                    return class_template_definition(
                        n, tree.children(seq)[0].index());
                /* [temp.class]/1's declaration without a definition
                   (template<class T> struct S;) */
                if (tree[seq].child_count == 1 &&
                    syntax_is(tree, tree.children(seq)[0],
                              syntax_kind::elaborated_type_specifier))
                    return class_template_declaration(
                        n, tree.children(seq)[0].index());
            }
            /* [temp.fct]/1: a function template's declaration
               (template<class T> T f(T);), one declarator not in an
               init-declarator-list */
            if (d.child_count == 3 &&
                (syntax_is(tree, tree.children(inner)[1],
                           syntax_kind::noptr_declarator) ||
                 syntax_is(tree, tree.children(inner)[1],
                           syntax_kind::ptr_declarator)))
                return function_template_definition(n, inner);
            return not_supported(n, "temp.class");
        }
        /* a function template's declaration or a variable template: which
           needs the declarator's type */
        return not_supported(n, "temp.pre");
    }
    default:
        return not_supported(n, "temp.pre");
    }
}

/* [temp.expl.spec]/1: "An explicit specialization of any of the following
   ... can be declared by a declaration introduced by template<>" */
analysis_result semantic_analysis::explicit_specialization(node_index n)
{
    /* template < > declaration: a class's definition, its class-head
       a simple-template-id */
    const syntax_node &x = tree[n];
    if (x.child_count == 4 &&
        syntax_is(tree, tree.children(n)[3], syntax_kind::simple_declaration)) {
        node_index inner = tree.children(n)[3].index();
        const syntax_node &d = tree[inner];
        if (d.child_count == 2 && syntax_is(tree, tree.children(inner)[0],
                                            syntax_kind::decl_specifier_seq)) {
            node_index seq = tree.children(inner)[0].index();
            if (tree[seq].child_count == 1 &&
                syntax_is(tree, tree.children(seq)[0],
                          syntax_kind::class_specifier)) {
                owned_array<node_index> none;
                return class_specialization_definition(
                    n, tree.children(seq)[0].index(), none);
            }
        }
    }
    /* ... or a function template's specialization, declared or defined */
    if (x.child_count != 4 || tree.children(n)[3].is_token())
        return not_supported(n, "temp.expl.spec");
    node_index inner = tree.children(n)[3].index();
    if (!syntax_is(tree, syntax_child::node(inner),
                   syntax_kind::function_definition) &&
        !syntax_is(tree, syntax_child::node(inner),
                   syntax_kind::simple_declaration))
        return not_supported(n, "temp.expl.spec");
    bool defines = tree[inner].kind == syntax_kind::function_definition;
    /* [dcl.fct.def.delete], = default: not yet */
    if (defines) {
        syntax_child body = tree.children(inner)[tree[inner].child_count - 1];
        if (!syntax_is(tree, body, syntax_kind::function_body) ||
            tree[body.index()].child_count != 1 ||
            !syntax_is(tree, tree.children(body.index())[0],
                       syntax_kind::compound_statement))
            return not_supported(n, "temp.expl.spec");
    }
    specified s;
    uint32_t index = 0;
    analysis_result r = named_specialization(n, inner, true, false, s, index);
    if (r != analysis_result::well_formed)
        return r;
    entity_index e = instantiations[index].specialization;
    if (!defines)
        return analysis_result::well_formed;
    if (entity_table[e].defined)
        /* [basic.def.odr]/1: a second definition */
        return not_supported(n, "basic.def.odr");
    /* the definition: the function-definition's declarator declares the
       specialization again (declare_specialization), its body analysed
       as any function's */
    entity_index saved_scope = current_scope;
    uint32_t saved_cv = function_cv;
    instantiating = index + 1;
    declarator_template_id = true;
    r = function_definition(inner);
    declarator_template_id = false;
    instantiating = 0;
    function_cv = saved_cv;
    current_scope = saved_scope;
    if (r == analysis_result::well_formed &&
        last_declared != instantiations[index].specialization)
        return not_supported(n, "temp.expl.spec");
    entity_table[e].defined = true;
    return r;
}

/* [temp.explicit]/1: "A class, function, variable, or member template
   specialization can be explicitly instantiated from its template." */
analysis_result semantic_analysis::explicit_instantiation(node_index n)
{
    /* [extern] template declaration */
    const syntax_node &x = tree[n];
    const syntax_child *c = tree.children(n);
    bool extern_ = x.child_count == 3;
    if ((x.child_count != 2 && x.child_count != 3) ||
        c[x.child_count - 1].is_token())
        return not_supported(n, "temp.explicit");
    /* [temp.explicit]/3: "An explicit instantiation shall appear in an
       enclosing namespace of its template": here, its own */
    if (!is_namespace(current_scope) || defining_class || function_state.active)
        return not_supported(n, "temp.explicit");
    node_index d = c[x.child_count - 1].index();
    if (tree[d].kind != syntax_kind::simple_declaration ||
        leading_gnu_attributes(d))
        return not_supported(n, "temp.explicit");
    const syntax_child *dc = tree.children(d);
    if (tree[d].child_count == 2 &&
        syntax_is(tree, dc[0], syntax_kind::decl_specifier_seq) &&
        tree[dc[0].index()].child_count == 1 &&
        syntax_is(tree, tree.children(dc[0].index())[0],
                  syntax_kind::elaborated_type_specifier)) {
        /* [temp.explicit]/3: "If the explicit instantiation is for a
           class or member class, the elaborated-type-specifier in the
           declaration shall include a simple-template-id": class-key
           simple-template-id */
        node_index e = tree.children(dc[0].index())[0].index();
        const syntax_child *ec = tree.children(e);
        if (tree[e].child_count != 2 || !ec[0].is_token() ||
            !syntax_is(tree, ec[1], syntax_kind::simple_template_id) ||
            (p.keyword_of(tokens[ec[0].index()]) != standard_keyword::struct_ &&
             p.keyword_of(tokens[ec[0].index()]) != standard_keyword::class_))
            return not_supported(n, "temp.explicit");
        return class_explicit_instantiation(n, ec[1].index(), extern_);
    }
    if (tree[d].child_count != 3 ||
        !syntax_is(tree, dc[0], syntax_kind::decl_specifier_seq) ||
        dc[1].is_token() ||
        !syntax_is_punctuator(tokens, dc[2],
                              preprocessing_op_or_punc::SEMICOLON))
        return not_supported(n, "temp.explicit");
    /* a member function of a class template's specialization:
       S<A>::f(...) */
    if (syntax_is(tree, dc[1], syntax_kind::noptr_declarator) &&
        tree[dc[1].index()].child_count &&
        syntax_is(tree, tree.children(dc[1].index())[0],
                  syntax_kind::qualified_id))
        return member_explicit_instantiation(n, d, extern_);
    specified s;
    uint32_t index = 0;
    analysis_result r = named_specialization(n, d, false, extern_, s, index);
    if (r != analysis_result::well_formed)
        return r;
    entity_index e = instantiations[index].specialization;
    if (extern_)
        entity_table[e].explicitly_instantiated =
            instantiation_kind::declaration;
    else
        entity_table[e].explicitly_instantiated =
            instantiation_kind::definition;
    return analysis_result::well_formed;
}

uint32_t semantic_analysis::find_instantiation(
    entity_index t, const owned_array<template_argument> &a) const
{
    for (uint32_t k = 0; k < instantiations.size(); k++) {
        const instantiation_record &x = instantiations[k];
        if (x.template_entity != t || x.argument_count != a.size())
            continue;
        bool same = true;
        for (uint32_t i = 0; i < x.argument_count && same; i++)
            same = template_arguments[x.first_argument + i] == a[i];
        if (same)
            return k + 1;
    }
    return 0;
}

/*
 * [temp.expl.spec]/1 and [temp.explicit]/3: the function template's
 * specialization the declaration names: its declarator-id the template's
 * name or a simple-template-id of it ([temp.arg.explicit]/3), the
 * arguments not given deduced from the declarator's function type
 * ([temp.deduct.decl]/1: "the function template ... whose type matches
 * the declared type"), which shall be the specialization's.  An explicit
 * instantiation declaration's specialization is declared, its definition
 * left to its uses; an explicit specialization makes the specialization,
 * which no instantiation defines then ([temp.expl.spec]/7: it shall be
 * declared before a use that would instantiate it).
 */
analysis_result
semantic_analysis::named_specialization(node_index n, node_index declaration,
                                        bool specialization, bool extern_,
                                        specified &s, uint32_t &index)
{
    index = 0;
    const char *rule = "temp.explicit";
    if (specialization)
        rule = "temp.expl.spec";
    const syntax_node &d = tree[declaration];
    const syntax_child *dc = tree.children(declaration);
    if (leading_gnu_attributes(declaration) || d.child_count != 3 ||
        !syntax_is(tree, dc[0], syntax_kind::decl_specifier_seq) ||
        dc[1].is_token() ||
        (!syntax_is(tree, dc[1], syntax_kind::noptr_declarator) &&
         !syntax_is(tree, dc[1], syntax_kind::ptr_declarator)))
        return not_supported(n, rule);
    if (d.kind == syntax_kind::function_definition &&
        !syntax_is(tree, dc[2], syntax_kind::function_body))
        return not_supported(n, rule);
    if (d.kind == syntax_kind::simple_declaration &&
        !syntax_is_punctuator(tokens, dc[2],
                              preprocessing_op_or_punc::SEMICOLON))
        return not_supported(n, rule);
    if (!is_namespace(current_scope) || defining_class || function_state.active)
        return not_supported(n, rule);
    /* the declared type; what the declarator records for claims is the
       specialization's, which the definition (if any) records again */
    size_t expressions_before = expressions.size();
    size_t uses_before = name_uses.size();
    entity_index outer = current_scope;
    uint32_t saved_cv = function_cv;
    owned_array<parameter_meaning> saved_parameters =
        static_cast<owned_array<parameter_meaning> &&>(function_parameters);
    declarator_meaning m;
    qualified_type type;
    analysis_result r = decl_specifier_seq(dc[0].index(), s, false);
    uint32_t cv = 0;
    if (r == analysis_result::well_formed) {
        type = s.type;
        function_cv = 0;
        function_parameters.clear();
        declarator_template_id = true;
        r = declarator(declaration, dc[1], type, m);
        declarator_template_id = false;
        cv = function_cv;
    }
    bool defaults = false;
    for (const parameter_meaning &p : function_parameters)
        if (p.default_argument.index())
            defaults = true;
    current_scope = outer;
    function_cv = saved_cv;
    function_parameters =
        static_cast<owned_array<parameter_meaning> &&>(saved_parameters);
    expressions.resize(expressions_before);
    name_uses.resize(uses_before);
    if (r != analysis_result::well_formed)
        return r;
    /* [temp.expl.spec]/14 and [temp.explicit]/2's kinds of declaration;
       default arguments are [dcl.fct.default]/4's error */
    if (!m.has_id || m.qualifier || m.qualified_namespace ||
        m.operator_function || m.literal_operator || cv || defaults ||
        type_table[type.unqualified()].kind != type_kind::function)
        return not_supported(n, rule);
    if (s.typedef_ || s.static_ || s.extern_ || s.gnu_flags || m.gnu_flags ||
        s.aligned || m.aligned || s.section || m.section)
        return not_supported(n, rule);
    /* [temp.explicit]/3: "An explicit instantiation of a function
       template ... shall not use the inline, constexpr, or consteval
       specifiers" */
    if (!specialization && (s.inline_ || s.constexpr_))
        return not_supported(n, rule);
    /* the template and the arguments given */
    token_index name = 0;
    uint32_t ft = 0;
    owned_array<template_argument> values;
    bool qualified = false;
    syntax_child callee = syntax_child::token(m.id);
    if (m.template_id)
        callee = syntax_child::node(m.template_id);
    else
        ft = function_template_of(
            lookup_unqualified(tokens.identifier(m.id), lookup_filter::any)
                .unknown_entity);
    if (m.template_id) {
        size_t before = name_uses.size();
        r = template_callee_parts(callee, name, ft, values, qualified);
        name_uses.resize(before);
        if (r != analysis_result::well_formed)
            return r;
    }
    if (!ft)
        return not_supported(n, rule);
    const function_template f = function_templates[ft - 1];
    const entity t = entity_table[f.entity];
    if (t.enclosing != current_scope || f.primary)
        return not_supported(n, rule);
    /* [temp.deduct.decl]/1: the arguments from the function type */
    owned_array<bool> set;
    uint32_t given = static_cast<uint32_t>(values.size());
    for (uint32_t i = 0; i < f.parameter_count; i++) {
        if (i >= given)
            values.append(template_argument());
        set.append(i < given);
    }
    if (!deduce_type(t.type, type, values, set, true))
        return not_supported(n, "temp.deduct.decl");
    r = default_template_arguments(n, ft, values, set);
    if (r != analysis_result::well_formed)
        return not_supported(n, "temp.deduct.decl");
    uint32_t prior = find_instantiation(f.entity, values);
    if (prior) {
        const instantiation_record &x = instantiations[prior - 1];
        entity_index e = x.specialization;
        if (!e)
            return not_supported(n, rule);
        if (specialization) {
            /* [temp.expl.spec]/7: a specialization used (or explicitly
               instantiated) before is clang's error; one declared by an
               explicit specialization before is declared again */
            if (!x.explicit_specialization ||
                entity_table[e].inline_ != (s.inline_ || s.constexpr_) ||
                entity_table[e].constexpr_ != s.constexpr_ ||
                !(entity_table[e].type == type))
                return not_supported(n, rule);
            entity_table.add_declaration(e, declaration);
            index = prior - 1;
            return analysis_result::well_formed;
        }
        /* [temp.explicit]/7, /11: after an explicit specialization, a
           second definition, a declaration after the definition or after
           a use: clang's diagnostics, or none */
        if (x.explicit_specialization ||
            entity_table[e].explicitly_instantiated ==
                instantiation_kind::definition ||
            (extern_ && (x.defined || x.pending ||
                         entity_table[e].explicitly_instantiated !=
                             instantiation_kind::declaration)))
            return not_supported(n, rule);
    }
    /* clang's "explicit instantiation of undefined function template" */
    if (!specialization && !extern_ && !f.definition)
        return not_supported(n, rule);
    entity_index e = 0;
    /* (an explicit specialization or an explicit instantiation
       declaration does not instantiate the definition) */
    if (specialization || extern_)
        unevaluated++;
    r = instantiate(n, f.entity, values.data(),
                    static_cast<uint32_t>(values.size()), e);
    if (specialization || extern_)
        unevaluated--;
    if (r != analysis_result::well_formed || !e)
        return not_supported(n, rule);
    if (!(entity_table[e].type == type))
        return not_supported(n, "temp.deduct.decl");
    index = entity_table[e].instantiation - 1;
    if (specialization) {
        /* [temp.expl.spec]/12: "An explicit specialization of a function
           or variable template is inline only if it is declared with the
           inline specifier or defined as deleted, and independently of
           whether its function or variable template is inline" */
        instantiations[index].explicit_specialization = true;
        instantiations[index].defined = true;
        /* ([dcl.constexpr]/1: "A function ... declared with a constexpr
           ... specifier is implicitly an inline function") */
        entity_table[e].inline_ = s.inline_ || s.constexpr_;
        entity_table[e].constexpr_ = s.constexpr_;
        /* its first declaration is this one, not the template's
           (clang's name of it, which a lookup claim gives) */
        entity_table[e].first_declaration = 0;
        entity_table[e].last_declaration = 0;
        entity_table.add_declaration(e, declaration);
        for (declared_name &x : declared)
            if (x.entity == e) {
                x.declaration = declaration;
                x.outermost = n;
                x.id = m.id;
            }
    }
    return analysis_result::well_formed;
}

/*
 * [temp.explicit]/10: "An explicit instantiation definition that names a
 * class template specialization explicitly instantiates the class
 * template specialization and is an explicit instantiation definition of
 * only those members that have been defined at the point of
 * instantiation."  The class is instantiated as for any use; its member
 * functions are then emitted (explicitly_instantiated), or, for an
 * explicit instantiation declaration (/11), not.
 */
analysis_result semantic_analysis::class_explicit_instantiation(node_index n,
                                                                node_index id,
                                                                bool extern_)
{
    const syntax_child *parts = tree.children(id);
    if (tree[id].child_count < 3 || !parts[0].is_token() ||
        tokens[parts[0].index()].kind != token_kind::identifier)
        return not_supported(n, "temp.explicit");
    lookup_result x = lookup_unqualified(tokens.identifier(parts[0].index()),
                                         lookup_filter::any);
    entity_index named = x.entity;
    if (!named)
        named = x.unknown_entity;
    uint32_t ct = class_template_of(named);
    if (!ct || entity_table[function_templates[ct - 1].entity].enclosing !=
                   current_scope)
        return not_supported(n, "temp.explicit");
    entity_index t = function_templates[ct - 1].entity;
    /* whether the specialization was named before (/11: a declaration
       after an implicit instantiation is left to clang) */
    size_t uses_before = name_uses.size();
    owned_array<template_argument> arguments;
    analysis_result r = template_id_arguments(id, ct, arguments);
    name_uses.resize(uses_before);
    if (r != analysis_result::well_formed)
        return not_supported(n, "temp.explicit");
    uint32_t prior = find_instantiation(t, arguments);
    type_index type = 0;
    r = template_type(id, 0, false, type);
    if (r != analysis_result::well_formed)
        return not_supported(n, "temp.explicit");
    const struct type &ty = type_table[type];
    if (ty.kind != type_kind::class_type)
        return not_supported(n, "temp.explicit");
    entity_index c = ty.entity;
    const entity &k = entity_table[c];
    if (!k.instantiation || !k.defined || k.union_)
        return not_supported(n, "temp.explicit");
    const instantiation_record record = instantiations[k.instantiation - 1];
    /* [temp.expl.spec]: an explicit specialization's class is no
       instantiation */
    if (record.pattern &&
        !function_templates[record.pattern - 1].parameter_count)
        return not_supported(n, "temp.explicit");
    /* /10's members defined outside the class: one that failed is not
       known to be well-formed for this class */
    for (const template_member_definition &m : template_member_definitions) {
        if (m.template_entity != t)
            continue;
        for (const analysed_declaration &a : declarations)
            if (a.node == m.declaration &&
                a.result != analysis_result::well_formed)
                return not_supported(n, "temp.explicit");
    }
    instantiation_kind was = entity_table[c].explicitly_instantiated;
    if (was == instantiation_kind::definition ||
        (extern_ && (was == instantiation_kind::none) && prior))
        return not_supported(n, "temp.explicit");
    if (extern_)
        entity_table[c].explicitly_instantiated =
            instantiation_kind::declaration;
    else
        entity_table[c].explicitly_instantiated =
            instantiation_kind::definition;
    return analysis_result::well_formed;
}

/*
 * [temp.explicit]/3: "If the explicit instantiation is for a function or
 * member function, the unqualified-id in the declarator shall be either a
 * template-id or, where all template arguments can be deduced, a
 * template-name or operator-function-id": here a member function of a
 * class template's specialization, S<A>::f(...), defined.
 */
analysis_result semantic_analysis::member_explicit_instantiation(
    node_index n, node_index declaration, bool extern_)
{
    const syntax_child *dc = tree.children(declaration);
    size_t expressions_before = expressions.size();
    size_t uses_before = name_uses.size();
    entity_index outer = current_scope;
    uint32_t saved_cv = function_cv;
    owned_array<parameter_meaning> saved_parameters =
        static_cast<owned_array<parameter_meaning> &&>(function_parameters);
    specified s;
    declarator_meaning m;
    qualified_type type;
    analysis_result r = decl_specifier_seq(dc[0].index(), s, false);
    uint32_t cv = 0;
    if (r == analysis_result::well_formed) {
        type = s.type;
        function_cv = 0;
        function_parameters.clear();
        r = declarator(declaration, dc[1], type, m);
        cv = function_cv;
    }
    bool defaults = false;
    for (const parameter_meaning &p : function_parameters)
        if (p.default_argument.index())
            defaults = true;
    current_scope = outer;
    function_cv = saved_cv;
    function_parameters =
        static_cast<owned_array<parameter_meaning> &&>(saved_parameters);
    expressions.resize(expressions_before);
    name_uses.resize(uses_before);
    if (r != analysis_result::well_formed)
        return not_supported(n, "temp.explicit");
    if (!m.has_id || !m.qualifier || m.operator_function ||
        m.literal_operator || m.destructor || defaults || s.typedef_ ||
        s.static_ || s.extern_ || s.inline_ || s.constexpr_ || s.gnu_flags ||
        m.gnu_flags || s.aligned || m.aligned || s.section || m.section ||
        type_table[type.unqualified()].kind != type_kind::function)
        return not_supported(n, "temp.explicit");
    const entity c = entity_table[m.qualifier];
    if (c.kind != entity_kind::class_type || !c.instantiation ||
        entity_table[c.enclosing].kind == entity_kind::class_type ||
        c.enclosing != current_scope)
        return not_supported(n, "temp.explicit");
    const instantiation_record record = instantiations[c.instantiation - 1];
    if (record.pattern &&
        !function_templates[record.pattern - 1].parameter_count)
        return not_supported(n, "temp.explicit");
    /* the member: one function of the name, of the declared type */
    entity_index f = entity_table.find(m.qualifier, tokens.identifier(m.id));
    if (!f || entity_table[f].kind != entity_kind::function ||
        entity_table[f].next_overload ||
        entity_table[f].enclosing != m.qualifier ||
        entity_table[f].special != special_member::none ||
        !(entity_table[f].type == type) || entity_table[f].member_cv != cv)
        return not_supported(n, "temp.explicit");
    /* clang's "explicit instantiation of undefined function" */
    if (!entity_table[f].defined)
        return not_supported(n, "temp.explicit");
    /* (a declaration: whether a use before it was emitted is left to
       clang) */
    instantiation_kind was = explicitly_instantiated(f);
    if (was == instantiation_kind::definition || extern_)
        return not_supported(n, "temp.explicit");
    if (extern_)
        entity_table[f].explicitly_instantiated =
            instantiation_kind::declaration;
    else
        entity_table[f].explicitly_instantiated =
            instantiation_kind::definition;
    return analysis_result::well_formed;
}

instantiation_kind
semantic_analysis::explicitly_instantiated(entity_index f) const
{
    const entity &x = entity_table[f];
    if (x.kind != entity_kind::function)
        return instantiation_kind::none;
    if (x.explicitly_instantiated != instantiation_kind::none)
        return x.explicitly_instantiated;
    /* /10: a class's member functions that are not special members
       (an implicitly-declared one is defined where used, [class.default.ctor]/4) */
    if (x.special != special_member::none || x.implicit)
        return instantiation_kind::none;
    for (entity_index c = x.enclosing;
         c && entity_table[c].kind == entity_kind::class_type;
         c = entity_table[c].enclosing)
        if (entity_table[c].explicitly_instantiated != instantiation_kind::none)
            return entity_table[c].explicitly_instantiated;
    return instantiation_kind::none;
}

/* [temp.deduct.guide]/1: "Deduction guides are used when a template-name
   appears as a type specifier for a deduced class type" */
analysis_result semantic_analysis::deduction_guide(node_index n)
{
    return not_supported(n, "temp.deduct.guide");
}

/*
 * [temp.pre]/1: the template-head's template-parameter-list: type
 * parameters of the form class identifier (typename identifier), with no
 * default, pack or type-constraint; a requires-clause is [temp.constr].
 * The template is one of namespace scope.
 */
analysis_result semantic_analysis::template_head_parameters(
    node_index n, owned_array<node_index> &parameters, bool packs)
{
    const syntax_node &node = tree[n];
    if (node.child_count != 2 ||
        !syntax_is(tree, tree.children(n)[0], syntax_kind::template_head))
        return not_supported(n, "temp.pre");
    if (!is_namespace(current_scope) || defining_class)
        return not_supported(n, "temp.mem");
    /* template-head: template < template-parameter-list >; a
       requires-clause is [temp.constr] */
    node_index head = tree.children(n)[0].index();
    for (uint32_t i = 0; i < tree[head].child_count; i++) {
        syntax_child c = tree.children(head)[i];
        if (c.is_token())
            continue;
        if (syntax_is(tree, c, syntax_kind::type_parameter) ||
            syntax_is(tree, c, syntax_kind::parameter_declaration)) {
            parameters.append(c.index());
            continue;
        }
        if (!syntax_is(tree, c, syntax_kind::template_parameter_list))
            return not_supported(n, "temp.constr");
        for (uint32_t k = 0; k < tree[c.index()].child_count; k++) {
            syntax_child p = tree.children(c.index())[k];
            if (p.is_token())
                continue;
            if (!syntax_is(tree, p, syntax_kind::type_parameter) &&
                !syntax_is(tree, p, syntax_kind::parameter_declaration))
                return not_supported(p.index(), "temp.param");
            parameters.append(p.index());
        }
    }
    /* a type-parameter class identifier, a parameter-declaration
       decl-specifier-seq identifier, each with = and a default
       template-argument or not: a pack, a type-constraint, an unnamed
       parameter or another declarator is a rule of its own */
    for (size_t i = 0; i < parameters.size(); i++) {
        node_index p = parameters[i];
        const syntax_node &x = tree[p];
        const syntax_child *c = tree.children(p);
        bool type =
            syntax_is(tree, syntax_child::node(p), syntax_kind::type_parameter);
        /* [temp.param]/18: "If a template-parameter is a type-parameter
           with an ellipsis prior to its optional identifier ... then the
           template-parameter is a template parameter pack"; class ...
           identifier here, the template's last parameter, and only a
           function template's */
        if (type && x.child_count == 3 &&
            syntax_is_punctuator(tokens, c[1],
                                 preprocessing_op_or_punc::ELLIPSIS) &&
            c[2].is_token() &&
            tokens[c[2].index()].kind == token_kind::identifier) {
            if (!packs || i + 1 != parameters.size())
                return not_supported(p, "temp.variadic");
            continue;
        }
        if (type != c[0].is_token() ||
            (x.child_count != 2 && x.child_count != 4) || !c[1].is_token() ||
            tokens[c[1].index()].kind != token_kind::identifier ||
            (x.child_count == 4 &&
             !syntax_is_punctuator(tokens, c[2],
                                   preprocessing_op_or_punc::EQUAL)))
            return not_supported(p, "temp.param");
    }
    if (parameters.empty())
        return not_supported(n, "temp.param");
    return analysis_result::well_formed;
}

static bool depends(const types &table, qualified_type t,
                    const entities *parameters = nullptr, uint64_t from = 0);

/*
 * [temp.fct]/1: "A function template defines an unbounded set of related
 * functions."  One defined at namespace scope with type parameters only
 * (template<class T, typename U> R f(P...) { ... }): its declarator is
 * analysed with each parameter's name a typedef-name of its
 * template-parameter type, which gives the template's function type (what
 * deduction compares a call's arguments with, and what the specialization's
 * name encodes, [abi:mangle.function-type]); its body is analysed per
 * specialization (instantiate).  The template's name stays the one
 * hold_names binds, which a call finds (call, expr.post.cpp); any other use
 * of it is [temp.names].
 */
analysis_result
semantic_analysis::function_template_definition(node_index n,
                                                node_index definition)
{
    entity_index outer = current_scope;
    owned_array<node_index> parameters;
    analysis_result head = template_head_parameters(n, parameters, true);
    if (head != analysis_result::well_formed)
        return head;
    const syntax_node &d = tree[definition];
    /* a function-definition, or a simple-declaration of one declarator
       without an initializer: the template declared, defined later */
    bool defines = d.kind == syntax_kind::function_definition;
    if (leading_gnu_attributes(definition) || d.child_count != 3 ||
        !syntax_is(tree, tree.children(definition)[0],
                   syntax_kind::decl_specifier_seq) ||
        tree.children(definition)[1].is_token() ||
        (defines && !syntax_is(tree, tree.children(definition)[2],
                               syntax_kind::function_body)) ||
        (!defines &&
         (!syntax_is_punctuator(tokens, tree.children(definition)[2],
                                preprocessing_op_or_punc::SEMICOLON) ||
          (!syntax_is(tree, tree.children(definition)[1],
                      syntax_kind::noptr_declarator) &&
           !syntax_is(tree, tree.children(definition)[1],
                      syntax_kind::ptr_declarator)))))
        return not_supported(n, "temp.fct");
    uint32_t index = static_cast<uint32_t>(function_templates.size());
    /* the scope of the template-parameters ([basic.scope.temp]) */
    entity_index scope = entity_table.make(entity_kind::block_scope, 0, outer);
    uint32_t first = static_cast<uint32_t>(template_parameter_entities.size());
    analysis_result declared =
        declare_template_parameters(n, parameters, scope, index, 0);
    if (declared != analysis_result::well_formed)
        return declared;
    /* the declarator, in the template-parameters' scope; what it records
       for claims is the template's, which clang has no claim for */
    size_t expressions_before = expressions.size();
    size_t uses_before = name_uses.size();
    current_scope = scope;
    specified s;
    declarator_meaning m;
    qualified_type type;
    analysis_result r =
        decl_specifier_seq(tree.children(definition)[0].index(), s, false);
    if (r == analysis_result::well_formed) {
        type = s.type;
        function_cv = 0;
        function_parameters.clear();
        r = declarator(definition, tree.children(definition)[1], type, m);
    }
    current_scope = outer;
    expressions.resize(expressions_before);
    name_uses.resize(uses_before);
    if (r != analysis_result::well_formed)
        return r;
    if (!m.has_id || m.qualifier || m.qualified_namespace ||
        m.operator_function || m.literal_operator || function_cv ||
        type_table[type.unqualified()].kind != type_kind::function)
        return not_supported(n, "temp.fct");
    if (s.typedef_ || s.extern_ || c_language || s.gnu_flags || m.gnu_flags ||
        s.aligned || m.aligned || s.section || m.section)
        return not_supported(n, "temp.fct");
    for (const parameter_meaning &p : function_parameters)
        if (p.default_argument.index())
            return not_supported(n, "dcl.fct.default");
    /* [temp.variadic]/7: "A pack whose name appears within the pattern of
       a pack expansion is expanded by that pack expansion"; a pack named
       outside one is ill-formed (/7), which the template's own declarator
       does not tell apart from a pattern analysis has no rules for: the
       last parameter's type the only pack expansion, a function parameter
       pack of the template parameter pack (the last) */
    {
        uint32_t last = static_cast<uint32_t>(parameters.size()) - 1;
        bool pack = template_parameter_is_pack(first + last);
        const struct type ft = type_table[type.unqualified()];
        const qualified_type *list = type_table.parameters(type.unqualified());
        if (pack && depends(type_table, ft.operand, &entity_table, last))
            return not_supported(n, "temp.variadic");
        for (uint32_t i = 0; i < ft.parameter_count; i++) {
            const struct type pt = type_table[list[i].unqualified()];
            if (pt.kind != type_kind::pack_expansion) {
                if (pack && depends(type_table, list[i], &entity_table, last))
                    return not_supported(n, "temp.variadic");
                continue;
            }
            if (!pack || i + 1 != ft.parameter_count || ft.variadic ||
                !depends(type_table, pt.operand, &entity_table, last))
                return not_supported(n, "temp.variadic");
        }
    }
    uint32_t name = tokens.identifier(m.id);
    /* another declaration of a template declared before
       ([temp.over.link]/6: equivalent template-heads and function types),
       the first's record */
    entity_index prior = entity_table.find(outer, name);
    uint32_t earlier = function_template_of(prior);
    if (prior && earlier) {
        const function_template f = function_templates[earlier - 1];
        const entity old = entity_table[f.entity];
        if (f.definition && defines)
            /* [basic.def.odr]/1: a second definition */
            return not_supported(n, "basic.def.odr");
        if (old.enclosing != outer || f.parameter_count != parameters.size() ||
            !same_template_parameters(first, f.first_parameter,
                                      f.parameter_count) ||
            old.inline_ != s.inline_ || old.constexpr_ != s.constexpr_ ||
            (s.static_ && old.linkage_of_name != linkage::internal))
            return not_supported(n, "temp.over.link");
        /* the same names: an instantiation made before the definition
           binds the first declaration's (instantiate) */
        for (uint32_t i = 0; i < f.parameter_count; i++)
            if (entity_table[template_parameter_entities[first + i]].name !=
                entity_table[template_parameter_entities[f.first_parameter + i]]
                    .name)
                return not_supported(n, "temp.over.link");
        /* the function types the same once each parameter is the other's
           (the old in terms of the new, exactly) */
        owned_array<template_argument> values;
        owned_array<bool> set;
        for (uint32_t i = 0; i < f.parameter_count; i++) {
            values.append(template_argument());
            set.append(false);
        }
        if (!deduce_type(old.type, type, values, set, true))
            return not_supported(n, "temp.over.link");
        for (uint32_t i = 0; i < f.parameter_count; i++) {
            entity_index pe = template_parameter_entities[first + i];
            if (set[i] && (entity_table[pe].kind != entity_kind::typedef_name ||
                           !(values[i].type == entity_table[pe].type)))
                return not_supported(n, "temp.over.link");
        }
        entity_table.add_declaration(f.entity, n);
        if (defines) {
            function_template &record = function_templates[earlier - 1];
            record.declaration = n;
            record.definition = definition;
            record.declarator = definition;
            entity_table[f.entity].defined = true;
        }
        return analysis_result::well_formed;
    }
    entity_index t =
        entity_table.make(entity_kind::template_entity, name, outer);
    entity_table[t].type = type;
    entity_table[t].defined = defines;
    entity_table[t].inline_ = s.inline_;
    entity_table[t].constexpr_ = s.constexpr_;
    entity_table[t].instantiation = index + 1;
    /* [basic.link]/3.1: a function template declared static has internal
       linkage, as does any name of an unnamed namespace (/4) */
    entity_table[t].linkage_of_name = linkage::external;
    if (s.static_)
        entity_table[t].linkage_of_name = linkage::internal;
    for (entity_index y = outer; y; y = entity_table[y].enclosing)
        if (entity_table[y].kind == entity_kind::namespace_entity &&
            !entity_table[y].name)
            entity_table[t].linkage_of_name = linkage::internal;
    entity_table.add_declaration(t, n);
    function_template record;
    record.entity = t;
    record.declaration = n;
    if (defines)
        record.definition = definition;
    record.declarator = definition;
    record.first_parameter = first;
    record.parameter_count = static_cast<uint32_t>(parameters.size());
    /* the name: hold_names binds a fresh one; one it finds bound already
       declares something else too (an overload, another template), which
       calls of it need [over.match.funcs] for */
    size_t before = entity_table.size();
    hold_names(n);
    entity_index held = entity_table.find(outer, name);
    record.name = held;
    if (!held || held < before ||
        entity_table[held].kind != entity_kind::none) {
        record.unusable = true;
        for (function_template &other : function_templates)
            if (other.name == held)
                other.unusable = true;
    }
    function_templates.append(record);
    return analysis_result::well_formed;
}

/*
 * [temp.param]/2: "There is no semantic difference between class and
 * typename in a type-parameter-key."  A type parameter's name is a
 * typedef-name of its template_parameter type; a non-type parameter's
 * (/4: of integral type here) names a value the template's own
 * declarator does not know: an entity of kind none with the parameter's
 * type, which an instantiation binds to a constant.
 */
analysis_result semantic_analysis::declare_template_parameters(
    node_index n, const owned_array<node_index> &parameters, entity_index scope,
    uint32_t index, uint32_t reserved)
{
    (void)n;
    for (size_t i = 0; i < parameters.size(); i++) {
        node_index node = parameters[i];
        token_index id = template_parameter_id(node);
        uint32_t name = tokens.identifier(id);
        /* [temp.local]/6: "The name of a template-parameter shall not be
           redeclared within its scope (including nested scopes)." */
        if (entity_table.find(scope, name) || name == reserved)
            return ill_formed_at_token(id,
                                       "the name of a template-parameter "
                                       "shall not be redeclared within its "
                                       "scope",
                                       "temp.local");
        entity_index e = 0;
        if (syntax_is(tree, syntax_child::node(node),
                      syntax_kind::type_parameter)) {
            e = entity_table.make(entity_kind::typedef_name, name, scope);
            entity_table[e].type = qualified_type::make(
                type_table.of_entity(type_kind::template_parameter, e),
                cv_none);
        } else {
            /* the parameter's type, in the scope of those before it */
            entity_index saved = current_scope;
            current_scope = scope;
            specified sp;
            analysis_result r =
                decl_specifier_seq(tree.children(node)[0].index(), sp, false);
            current_scope = saved;
            if (r != analysis_result::well_formed)
                return r;
            if (!is_integral(sp.type) || depends(type_table, sp.type) ||
                sp.typedef_ || sp.static_ || sp.extern_ || sp.constexpr_ ||
                sp.inline_)
                return not_supported(node, "temp.param");
            e = entity_table.make(entity_kind::none, name, scope);
            /* /6: "the top-level cv-qualifiers on the template-parameter
               are ignored when determining its type" */
            entity_table[e].type =
                qualified_type::make(sp.type.unqualified(), cv_none);
        }
        entity_table[e].value = i;
        entity_table[e].defined = true;
        entity_table[e].instantiation = index + 1;
        entity_table.bind(scope, name, e);
        template_parameter_entities.append(e);
        template_parameter_nodes.append(node);
    }
    return analysis_result::well_formed;
}

/* the parameter's name in scope as its argument: [temp.arg.type]'s type,
   or [temp.arg.nontype]'s value, a prvalue ([temp.param]/8) that
   enumerator_value gives as an enumerator's */
void semantic_analysis::bind_template_argument(entity_index scope,
                                               entity_index parameter,
                                               const template_argument &a)
{
    uint32_t name = entity_table[parameter].name;
    if (a.kind == template_argument::kind_t::pack) {
        /* [temp.variadic]/1: a pack of its elements' names */
        owned_array<entity_index> elements;
        for (uint32_t k = 0; k < a.count; k++) {
            const template_argument x = pack_arguments[a.value + k];
            entity_index e = 0;
            if (x.kind == template_argument::kind_t::value) {
                e = entity_table.make(entity_kind::enumerator, name, scope);
                entity_table[e].value = x.value;
            } else if (x.kind == template_argument::kind_t::type) {
                e = entity_table.make(entity_kind::typedef_name, name, scope);
            } else {
                /* (a value-dependent element: not made) */
                continue;
            }
            entity_table[e].type = x.type;
            entity_table[e].defined = true;
            elements.append(e);
        }
        make_bound_pack(scope, name, elements.data(),
                        static_cast<uint32_t>(elements.size()));
        return;
    }
    if (a.template_entity) {
        /* a value-dependent argument: the parameter it names */
        entity_table.bind(scope, name, a.template_entity);
        return;
    }
    entity_index e = 0;
    if (a.kind == template_argument::kind_t::value) {
        e = entity_table.make(entity_kind::enumerator, name, scope);
        entity_table[e].value = a.value;
    } else {
        e = entity_table.make(entity_kind::typedef_name, name, scope);
    }
    entity_table[e].type = a.type;
    entity_table[e].defined = true;
    entity_table.bind(scope, name, e);
}

/*
 * [temp.arg.type]/1: "A template-argument for a template-parameter which
 * is a type shall be a type-id."  [temp.arg.nontype]/2: "A template-argument
 * for a non-type template-parameter shall be a converted constant
 * expression of the type of the template-parameter" (integral here).
 */
analysis_result semantic_analysis::template_argument_for(
    node_index at, uint32_t template_index, uint32_t position,
    syntax_child written, const template_argument *before,
    template_argument &out)
{
    (void)before;
    const function_template ft = function_templates[template_index - 1];
    entity_index p = template_parameter_entities[ft.first_parameter + position];
    out = template_argument();
    if (entity_table[p].kind == entity_kind::typedef_name) {
        if (!syntax_is(tree, written, syntax_kind::type_id))
            return not_supported(at, "temp.arg");
        return type_id(written.index(), out.type);
    }
    if (syntax_is(tree, written, syntax_kind::type_id))
        return not_supported(at, "temp.arg");
    qualified_type to = entity_table[p].type;
    /* in a template's own declaration, a non-type template-parameter's
       name: a value dependent argument ([temp.dep.constexpr]/2), of a
       parameter of the same type */
    token_index first = written.index();
    token_index end = first + 1;
    if (!written.is_token()) {
        first = tree[written.index()].first_token;
        end = tree[written.index()].end_token;
    }
    if (end == first + 1 && tokens[first].kind == token_kind::identifier) {
        lookup_result x =
            lookup_unqualified(tokens.identifier(first), lookup_filter::any);
        entity_index named = x.entity;
        if (!named)
            named = x.unknown_entity;
        if (named && entity_table[named].kind == entity_kind::none &&
            entity_table[named].instantiation &&
            !entity_table[named].held_by_template) {
            if (!(entity_table[named].type == to))
                return not_supported(at, "temp.arg.nontype");
            out.kind = template_argument::kind_t::value;
            out.type = to;
            out.template_entity = named;
            out.value = entity_table[named].value;
            return analysis_result::well_formed;
        }
    }
    expression_meaning m;
    analysis_result r = expression(written, m);
    if (r == analysis_result::well_formed)
        r = operand_value(at, m);
    if (r != analysis_result::well_formed)
        return r;
    if (!is_integral(m.type))
        return not_supported(at, "temp.arg.nontype");
    bool constant = false;
    r = integral_constant_expression(at, m, constant);
    if (r == analysis_result::well_formed && constant)
        r = converted_constant_expression(at, m, fundamental_of(to), constant);
    if (r != analysis_result::well_formed)
        return r;
    if (!constant)
        return not_supported(at, "temp.arg.nontype");
    out.kind = template_argument::kind_t::value;
    out.type = to;
    out.value = m.value;
    return analysis_result::well_formed;
}

/*
 * [temp.arg]/4: "When default template-arguments are used, a template
 * argument list can be empty."  [temp.param]/12: each default
 * template-argument is analysed where the arguments before it name the
 * parameters before it.
 */
analysis_result semantic_analysis::default_template_arguments(
    node_index at, uint32_t template_index,
    owned_array<template_argument> &arguments, const owned_array<bool> &set)
{
    const function_template ft = function_templates[template_index - 1];
    entity_index t = ft.entity;
    entity_index saved = current_scope;
    token_index saved_horizon = lookup_horizon;
    for (uint32_t i = 0; i < ft.parameter_count; i++) {
        if (set[i])
            continue;
        node_index node = template_parameter_nodes[ft.first_parameter + i];
        if (tree[node].child_count != 4)
            return not_supported(at, "temp.arg");
        entity_index scope = entity_table.make(entity_kind::block_scope, 0,
                                               entity_table[t].enclosing);
        for (uint32_t k = 0; k < i; k++)
            bind_template_argument(
                scope, template_parameter_entities[ft.first_parameter + k],
                arguments[k]);
        current_scope = scope;
        lookup_horizon = tree[ft.declaration].first_token;
        template_argument a;
        analysis_result r = template_argument_for(
            at, template_index, i, tree.children(node)[3], arguments.data(), a);
        current_scope = saved;
        lookup_horizon = saved_horizon;
        if (r != analysis_result::well_formed)
            return r;
        arguments[i] = a;
    }
    return analysis_result::well_formed;
}

/* the usable function template whose name the held entity is, plus one */
uint32_t semantic_analysis::function_template_of(entity_index held) const
{
    for (size_t i = 0; i < function_templates.size(); i++)
        if (function_templates[i].name == held && held &&
            !function_templates[i].unusable && !function_templates[i].is_class)
            return static_cast<uint32_t>(i + 1);
    return 0;
}

uint32_t semantic_analysis::class_template_of(entity_index e) const
{
    if (!e)
        return 0;
    /* [temp.local]/1: "the injected-class-name can be used as a
       template-name or a type-name.  When it is used with a
       template-argument-list ... it refers to the specified class template
       specialization" */
    if (entity_table[e].kind == entity_kind::class_type &&
        entity_table[e].instantiation)
        return entity_table[specialized_template(e)].instantiation;
    for (size_t i = 0; i < function_templates.size(); i++)
        if (function_templates[i].name == e && function_templates[i].is_class &&
            !function_templates[i].unusable)
            return static_cast<uint32_t>(i + 1);
    return 0;
}

/* whether a type is made of a template-parameter type, of one at
   position from or after if from is given */
static bool depends(const types &table, qualified_type t,
                    const entities *parameters, uint64_t from)
{
    const struct type &r = table[t.unqualified()];
    switch (r.kind) {
    case type_kind::template_parameter:
        return !parameters || (*parameters)[r.entity].value >= from;
    case type_kind::dependent:
        /* typename Q::name depends on what Q does */
        if (parameters &&
            (*parameters)[r.entity].kind == entity_kind::dependent_member)
            return depends(
                table,
                qualified_type::make(
                    static_cast<type_index>((*parameters)[r.entity].value),
                    cv_none),
                parameters, from);
        return true;
    case type_kind::pointer:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
    case type_kind::array:
    case type_kind::pack_expansion:
        return depends(table, r.operand, parameters, from);
    case type_kind::function: {
        if (depends(table, r.operand, parameters, from))
            return true;
        const qualified_type *p = table.parameters(t.unqualified());
        for (uint32_t i = 0; i < r.parameter_count; i++)
            if (depends(table, p[i], parameters, from))
                return true;
        return false;
    }
    default:
        return false;
    }
}

bool semantic_analysis::depends_on_template_parameter(qualified_type t) const
{
    return depends(type_table, t);
}

/*
 * [temp.class]/1: "A class template defines the layout and operations for
 * an unbounded set of related types."  One defined at namespace scope
 * (template<class T> struct S { ... };): its class-specifier is analysed
 * per specialization (instantiate_class), the template's name bound to a
 * held entity that a simple-template-id finds (template_type).
 */
analysis_result
semantic_analysis::class_template_definition(node_index n, node_index specifier)
{
    entity_index outer = current_scope;
    owned_array<node_index> parameters;
    analysis_result r = template_head_parameters(n, parameters, true);
    if (r != analysis_result::well_formed)
        return r;
    /* class-head: class-key identifier base-clause_opt */
    const syntax_node &spec = tree[specifier];
    if (!spec.child_count ||
        !syntax_is(tree, tree.children(specifier)[0], syntax_kind::class_head))
        return not_supported(n, "temp.class");
    node_index head = tree.children(specifier)[0].index();
    if (tree[head].child_count >= 2 &&
        syntax_is(tree, tree.children(head)[1],
                  syntax_kind::simple_template_id)) {
        /* a partial specialization's pack (its arguments' pack
           expansions) */
        for (node_index p : parameters)
            if (tree[p].child_count == 3 &&
                syntax_is_punctuator(tokens, tree.children(p)[1],
                                     preprocessing_op_or_punc::ELLIPSIS))
                return not_supported(n, "temp.variadic");
        return class_specialization_definition(n, specifier, parameters);
    }
    if (tree[head].child_count < 2 || !tree.children(head)[1].is_token() ||
        tokens[tree.children(head)[1].index()].kind != token_kind::identifier)
        return not_supported(n, "temp.class");
    /* a specialization declares the friend
       functions its class defines in the template's namespace
       ([class.friend]/6), which a lookup there would find; friends have
       no rules yet */
    for (token_index i = spec.first_token; i < spec.end_token; i++)
        if (p.keyword_of(tokens[i]) == standard_keyword::friend_)
            return not_supported(n, "class.friend");
    uint32_t name = tokens.identifier(tree.children(head)[1].index());
    uint32_t index = static_cast<uint32_t>(function_templates.size());
    entity_index scope = entity_table.make(entity_kind::block_scope, 0, outer);
    uint32_t first = static_cast<uint32_t>(template_parameter_entities.size());
    r = declare_template_parameters(n, parameters, scope, index, name);
    if (r != analysis_result::well_formed)
        return r;
    /* the definition of a class template declared before
       (class_template_declaration): its record, defined now */
    entity_index prior = entity_table.find(outer, name);
    uint32_t declared = class_template_of(prior);
    if (prior && declared && function_templates[declared - 1].name == prior &&
        !function_templates[declared - 1].definition &&
        !function_templates[declared - 1].primary) {
        const function_template d = function_templates[declared - 1];
        if (entity_table[d.entity].enclosing != outer ||
            d.parameter_count != parameters.size() ||
            !same_template_parameters(first, d.first_parameter,
                                      d.parameter_count))
            return not_supported(n, "temp.class");
        /* [temp.param]/12: "A template-parameter shall not be given
           default arguments by two different declarations in the same
           scope"; /10: "The set of default template-arguments available
           for use is obtained by merging the default arguments from all
           prior declarations": the definition's parameters take the
           declaration's defaults, if their names are the same (the
           default's names are the definition's when analysed) */
        for (uint32_t i = 0; i < d.parameter_count; i++) {
            node_index before = template_parameter_nodes[d.first_parameter + i];
            if (tree[before].child_count != 4)
                continue;
            if (tree[parameters[i]].child_count == 4 ||
                entity_table[template_parameter_entities[first + i]].name !=
                    entity_table[template_parameter_entities[d.first_parameter +
                                                             i]]
                        .name)
                return not_supported(n, "temp.param");
            template_parameter_nodes[first + i] = before;
        }
        /* [temp.param]/14 on the merged defaults (clang's error is the
           definition's) */
        for (uint32_t i = 1; i < d.parameter_count; i++)
            if (tree[template_parameter_nodes[first + i - 1]].child_count ==
                    4 &&
                tree[template_parameter_nodes[first + i]].child_count != 4)
                return not_supported(n, "temp.param");
        for (uint32_t i = 0; i < d.parameter_count; i++)
            entity_table[template_parameter_entities[first + i]].instantiation =
                declared;
        function_template &record = function_templates[declared - 1];
        record.declaration = n;
        record.definition = specifier;
        record.first_parameter = first;
        entity_table[d.entity].defined = true;
        entity_table.add_declaration(d.entity, n);
        return analysis_result::well_formed;
    }
    /* [temp.param]/14: "If a template-parameter of a class template ...
       has a default template-argument, each subsequent template-parameter
       shall either have a default template-argument supplied or be a
       template parameter pack" */
    for (size_t i = 1; i < parameters.size(); i++)
        if (tree[parameters[i - 1]].child_count == 4 &&
            tree[parameters[i]].child_count != 4 &&
            !template_parameter_is_pack(first + static_cast<uint32_t>(i)))
            return ill_formed(parameters[i],
                              "each template-parameter after one with a "
                              "default template-argument shall have one",
                              "temp.param");
    entity_index t =
        entity_table.make(entity_kind::template_entity, name, outer);
    entity_table[t].defined = true;
    entity_table[t].instantiation = index + 1;
    entity_table[t].linkage_of_name = linkage::external;
    if (within_unnamed_namespace(outer))
        entity_table[t].linkage_of_name = linkage::internal;
    entity_table.add_declaration(t, n);
    function_template record;
    record.entity = t;
    record.declaration = n;
    record.definition = specifier;
    record.first_parameter = first;
    record.parameter_count = static_cast<uint32_t>(parameters.size());
    record.is_class = true;
    /* the name, held: one bound already declares something else too (a
       class, another template's forward declaration) */
    entity_index held = entity_table.find(outer, name);
    if (held) {
        record.unusable = true;
        entity_table[held].kind = entity_kind::none;
        entity_table[held].held_by_template = true;
        for (function_template &other : function_templates)
            if (other.name == held)
                other.unusable = true;
    } else {
        held = entity_table.make(entity_kind::none, name, outer);
        entity_table[held].held_by_template = true;
        entity_table.bind(outer, name, held);
    }
    record.name = held;
    function_templates.append(record);
    return analysis_result::well_formed;
}

/*
 * [temp.class.spec]/1: "A partial specialization of a class template
 * provides an alternative definition of the template that is used instead
 * of the primary definition when the arguments in a specialization match
 * those given in the partial specialization."  [temp.expl.spec]/1: template<>
 * with no parameters, the class of those arguments.  A namespace-scope
 * class's definition whose class-head names the primary template, found by
 * the template-id's name, with arguments in terms of the specialization's
 * parameters; instantiate_class chooses it (matching_specialization).
 */
analysis_result semantic_analysis::class_specialization_definition(
    node_index n, node_index specifier,
    const owned_array<node_index> &parameters)
{
    const char *rule = "temp.class.spec";
    if (parameters.empty())
        rule = "temp.expl.spec";
    entity_index outer = current_scope;
    if (!is_namespace(outer) || defining_class)
        return not_supported(n, rule);
    const syntax_node &spec = tree[specifier];
    node_index head = tree.children(specifier)[0].index();
    node_index id = tree.children(head)[1].index();
    const syntax_child *parts = tree.children(id);
    if (tree[id].child_count < 3 || !parts[0].is_token() ||
        tokens[parts[0].index()].kind != token_kind::identifier)
        return not_supported(n, rule);
    for (token_index i = spec.first_token; i < spec.end_token; i++)
        if (p.keyword_of(tokens[i]) == standard_keyword::friend_)
            return not_supported(n, "class.friend");
    lookup_result x = lookup_unqualified(tokens.identifier(parts[0].index()),
                                         lookup_filter::any);
    entity_index named = x.entity;
    if (!named)
        named = x.unknown_entity;
    uint32_t ct = class_template_of(named);
    /* [temp.class.spec]/6, [temp.expl.spec]/3: declared where the
       primary template is, by its name (not an injected-class-name) */
    if (!ct || function_templates[ct - 1].name != named ||
        entity_table[function_templates[ct - 1].entity].enclosing != outer)
        return not_supported(n, rule);
    const function_template primary = function_templates[ct - 1];
    /* a variadic primary's: its arguments' packs are matched exactly, not
       deduced ([temp.class.spec.match]/2 with [temp.deduct.type]/9) */
    if (!parameters.empty() &&
        template_parameter_is_pack(primary.first_parameter +
                                   primary.parameter_count - 1))
        return not_supported(n, "temp.variadic");
    /* [temp.expl.spec]/7, [temp.class.spec]/1: one declared after a
       specialization it would be used for was instantiated makes the
       program ill-formed (no diagnostic required); any instantiation
       before it is not analysed */
    for (const instantiation_record &r : instantiations)
        if (r.template_entity == primary.entity && r.specialization &&
            entity_table[r.specialization].kind == entity_kind::class_type)
            return not_supported(n, rule);
    uint32_t index = static_cast<uint32_t>(function_templates.size());
    entity_index scope = entity_table.make(entity_kind::block_scope, 0, outer);
    uint32_t first = static_cast<uint32_t>(template_parameter_entities.size());
    analysis_result r =
        declare_template_parameters(n, parameters, scope, index, 0);
    if (r != analysis_result::well_formed)
        return r;
    /* the arguments, in the parameters' scope; what they record for
       claims is the template's */
    size_t expressions_before = expressions.size();
    size_t uses_before = name_uses.size();
    owned_array<template_argument> arguments;
    current_scope = scope;
    r = template_id_arguments(id, ct, arguments);
    current_scope = outer;
    expressions.resize(expressions_before);
    name_uses.resize(uses_before);
    if (r != analysis_result::well_formed)
        return r;
    bool dependent = false;
    bool identity = arguments.size() == parameters.size();
    for (uint32_t i = 0; i < arguments.size(); i++) {
        const template_argument &a = arguments[i];
        if (depends(type_table, a.type) || a.template_entity)
            dependent = true;
    }
    for (uint32_t i = 0; i < arguments.size() && identity; i++) {
        entity_index pe = template_parameter_entities[first + i];
        const template_argument &a = arguments[i];
        identity = a.template_entity == pe ||
                   (a.kind == template_argument::kind_t::type &&
                    a.type == entity_table[pe].type);
    }
    /* [temp.expl.spec]: no dependent argument; [temp.class.spec]/9: "The
       specialization shall be more specialized than the primary template"
       (the primary's own arguments are not; other cases are
       [temp.class.order]'s, not analysed) */
    if (parameters.empty() == dependent || identity)
        return not_supported(n, rule);
    function_template record;
    record.entity = primary.entity;
    record.declaration = n;
    record.definition = specifier;
    record.first_parameter = first;
    record.parameter_count = static_cast<uint32_t>(parameters.size());
    record.is_class = true;
    record.primary = ct;
    record.first_pattern = static_cast<uint32_t>(template_arguments.size());
    record.pattern_count = static_cast<uint32_t>(arguments.size());
    /* a second explicit specialization of the same arguments: a
       redefinition ([basic.def.odr]/1) */
    for (const function_template &o : function_templates) {
        if (o.primary != ct || o.parameter_count || !parameters.empty())
            continue;
        bool same = true;
        for (uint32_t i = 0; i < o.pattern_count && same; i++)
            same = template_arguments[o.first_pattern + i] == arguments[i];
        if (same)
            return not_supported(n, rule);
    }
    for (const template_argument &a : arguments)
        template_arguments.append(a);
    function_templates.append(record);
    return analysis_result::well_formed;
}

/*
 * [temp.class.spec.match]/1: "When a class template is used in a context
 * that requires an instantiation of the class, it is necessary to
 * determine whether the instantiation is to be generated using the primary
 * template or one of the partial specializations."  /2: "A partial
 * specialization matches a given actual template argument list if the
 * template arguments of the partial specialization can be deduced from the
 * actual template argument list" (exactly: [temp.deduct.type]/1's "make P
 * ... identical to A").  An explicit specialization of the arguments is
 * the specialization itself.  More than one partial specialization
 * matching is /1.2's partial ordering ([temp.class.order]): not
 * analysed.
 */
analysis_result semantic_analysis::matching_specialization(
    node_index at, uint32_t class_template, const template_argument *arguments,
    uint32_t count, uint32_t &pattern, owned_array<template_argument> &deduced)
{
    pattern = 0;
    deduced.clear();
    uint32_t matches = 0;
    for (uint32_t k = 0; k < function_templates.size(); k++) {
        const function_template s = function_templates[k];
        if (s.primary != class_template || s.pattern_count != count)
            continue;
        owned_array<template_argument> d;
        owned_array<bool> set;
        for (uint32_t i = 0; i < s.parameter_count; i++) {
            d.append(template_argument());
            set.append(false);
        }
        bool match = true;
        for (uint32_t i = 0; i < count && match; i++) {
            const template_argument x = template_arguments[s.first_pattern + i];
            const template_argument &y = arguments[i];
            if (x.kind != y.kind)
                match = false;
            else if (x.kind == template_argument::kind_t::type)
                match = deduce_type(x.type, y.type, d, set, true);
            else if (!x.template_entity)
                match = x == y;
            else if (set[x.value])
                match = d[x.value] == y;
            else {
                d[x.value] = y;
                set[x.value] = true;
            }
        }
        if (!match)
            continue;
        if (!s.parameter_count) {
            pattern = k + 1;
            deduced.clear();
            return analysis_result::well_formed;
        }
        /* [temp.class.spec.match]/2: a parameter the arguments do not
           determine is not deduced (clang's "can never be used") */
        for (uint32_t i = 0; i < s.parameter_count; i++)
            if (!set[i])
                return not_supported(at, "temp.class.spec");
        matches++;
        pattern = k + 1;
        deduced.clear();
        for (const template_argument &a : d)
            deduced.append(a);
    }
    if (matches > 1)
        return not_supported(at, "temp.class.order");
    return analysis_result::well_formed;
}

/*
 * [temp.names]/1 with [temp.arg.type]/1: template-name <
 * template-argument-list > naming a class template, its arguments type-ids;
 * the specialization instantiated ([temp.inst]/2), its type the type.
 */
analysis_result semantic_analysis::template_type(node_index id,
                                                 entity_index scope,
                                                 bool qualified,
                                                 type_index &type)
{
    const syntax_node &t = tree[id];
    const syntax_child *parts = tree.children(id);
    if (t.child_count < 3 || !parts[0].is_token() ||
        tokens[parts[0].index()].kind != token_kind::identifier)
        return not_supported(id, "temp.names");
    uint32_t name = tokens.identifier(parts[0].index());
    lookup_result x;
    if (qualified)
        x = lookup_qualified(scope, name, lookup_filter::any);
    else
        x = lookup_unqualified(name, lookup_filter::any);
    entity_index named = x.entity;
    if (!named)
        named = x.unknown_entity;
    uint32_t ct = class_template_of(named);
    if (!ct)
        return not_supported(id, "temp.names");
    owned_array<template_argument> arguments;
    analysis_result filled = template_id_arguments(id, ct, arguments);
    if (filled != analysis_result::well_formed)
        return filled;
    bool dependent = false;
    for (const template_argument &a : arguments) {
        if (depends(type_table, a.type) || a.template_entity)
            dependent = true;
        /* a pack of dependent arguments: deduction from it
           ([temp.deduct.type]/9) is not analysed */
        if (a.kind == template_argument::kind_t::pack)
            for (uint32_t k = 0; k < a.count; k++)
                if (depends(type_table, pack_arguments[a.value + k].type) ||
                    pack_arguments[a.value + k].template_entity)
                    return not_supported(id, "temp.variadic");
    }
    if (dependent) {
        /* [temp.dep.type]/9: "A type is dependent if it is ... a
           simple-template-id in which either the template name is a
           template parameter or any of the template arguments is a
           dependent type": a template's own declarator names it (not
           instantiated); one per template and arguments */
        entity_index t = function_templates[ct - 1].entity;
        for (const instantiation_record &x : instantiations) {
            if (x.template_entity != t || x.argument_count != arguments.size())
                continue;
            bool same = true;
            for (uint32_t i = 0; i < x.argument_count && same; i++)
                same = template_arguments[x.first_argument + i] == arguments[i];
            if (same) {
                type = entity_table[x.specialization].type.unqualified();
                return analysis_result::well_formed;
            }
        }
        instantiation_record record;
        record.template_entity = t;
        record.first_argument =
            static_cast<uint32_t>(template_arguments.size());
        record.argument_count = static_cast<uint32_t>(arguments.size());
        for (const template_argument &a : arguments)
            template_arguments.append(a);
        uint32_t index = static_cast<uint32_t>(instantiations.size());
        entity_index e =
            entity_table.make(entity_kind::template_specialization,
                              entity_table[t].name, entity_table[t].enclosing);
        entity_table[e].type = qualified_type::make(
            type_table.of_entity(type_kind::dependent, e), cv_none);
        entity_table[e].instantiation = index + 1;
        entity_table[e].linkage_of_name = entity_table[t].linkage_of_name;
        record.specialization = e;
        instantiations.append(record);
        type = entity_table[e].type.unqualified();
        return analysis_result::well_formed;
    }
    entity_index c = 0;
    analysis_result r = instantiate_class(
        id, ct, &arguments[0], static_cast<uint32_t>(arguments.size()), c);
    if (r != analysis_result::well_formed)
        return r;
    type = entity_table[c].type.unqualified();
    return analysis_result::well_formed;
}

/*
 * [temp.deduct.type]/1: "Template arguments can be deduced in several
 * different contexts, but in each case a type that is specified in terms
 * of template parameters (call it P) is compared with an actual type (call
 * it A), and an attempt is made to find template argument values ... that
 * will make P, after substitution of the deduced values (call it the
 * deduced A), compatible with A."  /8's forms T, cv T, T*, T&, T&&, T[N]
 * and function types.  False when P and A do not match or deductions
 * conflict.
 */
bool semantic_analysis::deduce_type(qualified_type p, qualified_type a,
                                    owned_array<template_argument> &deduced,
                                    owned_array<bool> &set, bool exact) const
{
    const struct type &pr = type_table[p.unqualified()];
    if (pr.kind == type_kind::template_parameter) {
        /* (P's cv-qualifiers beyond A's: [temp.deduct.call]/4.1's "more
           cv-qualified" deduced A, or 4.2's qualification conversion; the
           call's conversions check the rest) */
        if (exact && (p.cv() & ~a.cv()))
            return false;
        qualified_type value =
            qualified_type::make(a.unqualified(), a.cv() & ~p.cv());
        uint64_t i = entity_table[pr.entity].value;
        if (i >= deduced.size())
            return false;
        if (set[i] && !(deduced[i].type == value))
            return false;
        deduced[i].type = value;
        set[i] = true;
        return true;
    }
    if (!depends(type_table, p))
        return p == a;
    if (pr.kind == type_kind::dependent &&
        entity_table[pr.entity].kind == entity_kind::dependent_member)
        /* [temp.deduct.type]/5.1: "The nested-name-specifier of a type that
           was specified using a qualified-id" is a non-deduced context;
           the argument's conversion is checked on the specialization */
        return true;
    if (pr.kind == type_kind::dependent) {
        /* [temp.deduct.type]/8's TT<T>: a class template's specialization,
           whose arguments are deduced from those of A's, a
           specialization of the same template */
        const struct type &ar = type_table[a.unqualified()];
        /* ([temp.deduct.call]/4.1: P may be more cv-qualified) */
        if (ar.kind != type_kind::class_type || (a.cv() & ~p.cv()) ||
            (exact && p.cv() != a.cv()) ||
            !entity_table[ar.entity].instantiation ||
            specialized_template(ar.entity) != specialized_template(pr.entity))
            return false;
        uint32_t count = specialization_argument_count(pr.entity);
        for (uint32_t i = 0; i < count; i++) {
            const template_argument &x =
                specialization_template_argument(pr.entity, i);
            const template_argument &y =
                specialization_template_argument(ar.entity, i);
            if (x.kind == template_argument::kind_t::type) {
                if (!deduce_type(x.type, y.type, deduced, set, exact))
                    return false;
                continue;
            }
            if (!x.template_entity) {
                if (!(x == y))
                    return false;
                continue;
            }
            /* /8's TT<i>: a non-type parameter's value from A's (of the
               parameter's type: the template's parameter is the same) */
            uint64_t k = x.value;
            if (k >= deduced.size() || y.kind != x.kind)
                return false;
            if (set[k] && !(deduced[k] == y))
                return false;
            deduced[k] = y;
            set[k] = true;
        }
        return true;
    }
    const struct type &ar = type_table[a.unqualified()];
    if (pr.kind != ar.kind || p.cv() != a.cv())
        return false;
    switch (pr.kind) {
    case type_kind::pointer:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
    case type_kind::pack_expansion:
        return deduce_type(pr.operand, ar.operand, deduced, set, exact);
    case type_kind::array:
        if (pr.bound_known != ar.bound_known || pr.bound != ar.bound)
            return false;
        return deduce_type(pr.operand, ar.operand, deduced, set, exact);
    case type_kind::function: {
        if (pr.variadic != ar.variadic ||
            pr.parameter_count != ar.parameter_count)
            return false;
        if (!deduce_type(pr.operand, ar.operand, deduced, set, exact))
            return false;
        const qualified_type *pp = type_table.parameters(p.unqualified());
        const qualified_type *ap = type_table.parameters(a.unqualified());
        for (uint32_t i = 0; i < pr.parameter_count; i++)
            if (!deduce_type(pp[i], ap[i], deduced, set, exact))
                return false;
        return true;
    }
    default:
        return false;
    }
}

/*
 * [temp.deduct]/1: "When a function template specialization is referenced,
 * all of the template arguments shall have values.  The values can be
 * explicitly specified or, in some cases, be deduced from the use or
 * obtained from default template-arguments."  [temp.deduct.call]: from a
 * call's arguments.  No rules yet.
 */
analysis_result
semantic_analysis::deduce_from_call(node_index at, uint32_t function_template,
                                    const expression_meaning *arguments,
                                    uint32_t count,
                                    owned_array<template_argument> &deduced)
{
    const struct function_template &ft =
        function_templates[function_template - 1];
    qualified_type type = entity_table[ft.entity].type;
    const struct type f = type_table[type.unqualified()];
    const qualified_type *parameters =
        type_table.parameters(type.unqualified());
    /* [temp.deduct.call]/1: "For a function parameter pack that occurs at
       the end of the parameter-declaration-list, deduction is performed
       for each remaining argument of the call, taking the type P of the
       declarator-id of the function parameter pack as the corresponding
       function template parameter type.  Each deduction deduces template
       arguments for subsequent positions in the template parameter packs
       expanded by the function parameter pack." */
    uint32_t fixed = f.parameter_count;
    if (fixed && type_table[parameters[fixed - 1].unqualified()].kind ==
                     type_kind::pack_expansion)
        fixed--;
    if (f.variadic || count < fixed ||
        (fixed == f.parameter_count && count != fixed))
        return not_supported(at, "temp.deduct.call");
    uint32_t pack = ft.parameter_count;
    if (ft.parameter_count &&
        template_parameter_is_pack(ft.first_parameter + ft.parameter_count - 1))
        pack = ft.parameter_count - 1;
    /* [temp.arg.explicit]/3: "A template argument list may be specified
       when referring to a specialization of a function template"; /6:
       "The template argument list shall not specify more
       template-arguments than there are corresponding
       template-parameters" (clang: no candidate) */
    uint32_t specified = static_cast<uint32_t>(deduced.size());
    if (specified > ft.parameter_count || specified > pack)
        /* (arguments of a pack given: [temp.arg.explicit]/9's sequence,
           extended by deduction) */
        return not_supported(at, "temp.arg.explicit");
    owned_array<template_argument> values;
    owned_array<bool> set;
    for (uint32_t i = 0; i < ft.parameter_count; i++) {
        template_argument v;
        if (i < specified)
            v = deduced[i];
        values.append(v);
        set.append(i < specified);
    }
    owned_array<template_argument> elements;
    for (uint32_t i = 0; i < count; i++) {
        qualified_type p = parameters[i];
        if (i >= fixed)
            p = type_table[parameters[fixed].unqualified()].operand;
        /* /7: "Implicit conversions will be performed on a function
           argument to convert it to the type of the corresponding function
           parameter if the parameter type contains no template-parameters
           that participate in template argument deduction", which those
           explicitly specified do not */
        if (!depends(type_table, p, &entity_table, specified))
            continue;
        qualified_type a = arguments[i].type;
        const struct type ar = type_table[a.unqualified()];
        if (ar.kind == type_kind::none || ar.kind == type_kind::placeholder ||
            ar.kind == type_kind::dependent)
            return not_supported(at, "temp.deduct.call");
        const struct type pr = type_table[p.unqualified()];
        if (pr.kind == type_kind::rvalue_reference &&
            type_table[pr.operand.unqualified()].kind ==
                type_kind::template_parameter &&
            !pr.operand.cv() &&
            arguments[i].category == value_category::lvalue) {
            /* [temp.deduct.call]/3: "A forwarding reference is an rvalue
               reference to a cv-unqualified template parameter ... If P
               is a forwarding reference and the argument is an lvalue,
               the type "lvalue reference to A" is used in place of A for
               type deduction." */
            p = pr.operand;
            a = qualified_type::make(type_table.lvalue_reference_to(a),
                                     cv_none);
        } else if (pr.kind == type_kind::lvalue_reference ||
                   pr.kind == type_kind::rvalue_reference) {
            /* /3: "If P is a reference type, the type referred to by P is
               used for type deduction." */
            p = pr.operand;
        } else {
            /* /2: "If P is not a reference type: If A is an array type,
               the pointer type produced by the array-to-pointer standard
               conversion is used in place of A for type deduction;
               otherwise, If A is a function type, the pointer type
               produced by the function-to-pointer standard conversion is
               used in place of A for type deduction; otherwise, If A is a
               cv-qualified type, the top-level cv-qualifiers of A's type
               are ignored for type deduction."  /3: "If P is a
               cv-qualified type, the top-level cv-qualifiers of P's type
               are ignored" */
            if (ar.kind == type_kind::array)
                a = qualified_type::make(type_table.pointer_to(ar.operand),
                                         cv_none);
            else if (ar.kind == type_kind::function)
                a = qualified_type::make(type_table.pointer_to(a), cv_none);
            else
                a = qualified_type::make(a.unqualified(), cv_none);
            p = qualified_type::make(p.unqualified(), cv_none);
        }
        /* a parameter that does not match: deduction fails, which
           overload resolution of the function templates of the name and
           its other functions decides; neither is analysed yet */
        if (i < fixed) {
            if (!deduce_type(p, a, values, set))
                return not_supported(at, "temp.deduct.call");
            continue;
        }
        /* the pack's next element, the other parameters the pattern names
           the same in each */
        owned_array<template_argument> one;
        owned_array<bool> one_set;
        for (uint32_t k = 0; k < values.size(); k++) {
            one.append(values[k]);
            one_set.append(set[k] && k != pack);
        }
        if (!deduce_type(p, a, one, one_set) || pack >= one.size() ||
            !one_set[pack])
            return not_supported(at, "temp.deduct.call");
        for (uint32_t k = 0; k < values.size(); k++)
            if (k != pack && one_set[k] && !set[k]) {
                values[k] = one[k];
                set[k] = true;
            }
        elements.append(one[pack]);
    }
    /* [temp.arg.explicit]/4: "A trailing template parameter pack not
       otherwise deduced will be deduced as an empty sequence of template
       arguments." */
    if (pack < ft.parameter_count) {
        values[pack] =
            make_pack(elements.data(), static_cast<uint32_t>(elements.size()));
        set[pack] = true;
    }
    /* [temp.deduct]/5: "If a template argument has not been deduced and
       its corresponding template parameter has a default argument, the
       template argument is determined by substituting the template
       arguments determined for preceding template parameters into the
       default argument"; /1's "all of the template arguments shall have
       values" otherwise */
    analysis_result r =
        default_template_arguments(at, function_template, values, set);
    if (r != analysis_result::well_formed)
        return r;
    deduced.clear();
    for (const template_argument &x : values)
        deduced.append(x);
    return analysis_result::well_formed;
}

/*
 * [temp.names]/1: a simple-template-id's template-arguments for the class
 * template function_templates[ct - 1], in order, then [temp.arg]/4's
 * defaults for those not given.
 */
analysis_result semantic_analysis::template_id_arguments(
    node_index id, uint32_t ct, owned_array<template_argument> &arguments)
{
    const syntax_child *parts = tree.children(id);
    const function_template ft = function_templates[ct - 1];
    uint32_t count = ft.parameter_count;
    /* [temp.arg]/1: "When the parameter declared by the template is a
       template parameter pack, it will correspond to zero or more
       template-arguments": the last parameter's, the arguments from its
       position on */
    uint32_t pack = count;
    if (count && template_parameter_is_pack(ft.first_parameter + count - 1))
        pack = count - 1;
    owned_array<template_argument> elements;
    owned_array<syntax_child> written;
    owned_array<bool> expansion;
    for (uint32_t i = 2; i + 1 < tree[id].child_count; i++) {
        if (syntax_is_punctuator(tokens, parts[i],
                                 preprocessing_op_or_punc::ELLIPSIS)) {
            if (written.empty() || expansion.back())
                return not_supported(id, "temp.variadic");
            expansion.back() = true;
            continue;
        }
        if (syntax_is(tree, parts[i], syntax_kind::template_argument_list)) {
            for (uint32_t k = 0; k < tree[parts[i].index()].child_count; k++) {
                syntax_child a = tree.children(parts[i].index())[k];
                if (syntax_is_punctuator(tokens, a,
                                         preprocessing_op_or_punc::COMMA))
                    continue;
                if (syntax_is_punctuator(tokens, a,
                                         preprocessing_op_or_punc::ELLIPSIS)) {
                    if (written.empty() || expansion.back())
                        return not_supported(id, "temp.variadic");
                    expansion.back() = true;
                    continue;
                }
                written.append(a);
                expansion.append(false);
            }
        } else {
            written.append(parts[i]);
            expansion.append(false);
        }
    }
    /* a type-id that ends in the ellipsis: its pattern the type-id before
       it */
    for (size_t w = 0; w < written.size(); w++)
        if (syntax_is(tree, written[w], syntax_kind::type_id) &&
            tree[written[w].index()].child_count == 2 &&
            syntax_is_punctuator(tokens, tree.children(written[w].index())[1],
                                 preprocessing_op_or_punc::ELLIPSIS)) {
            if (expansion[w])
                return not_supported(id, "temp.variadic");
            expansion[w] = true;
        }
    {
        for (size_t w = 0; w < written.size(); w++) {
            syntax_child a = written[w];
            /* [temp.variadic]/5: a pack expansion's pattern, once per
               element of an instantiation's packs (a template's own
               pack expansion is not analysed) */
            uint32_t times = 1;
            if (expansion[w]) {
                token_index first = first_token(a);
                token_index end = first + 1;
                if (!a.is_token())
                    end = tree[a.index()].end_token;
                if (end > first + 1 &&
                    syntax_is_punctuator(tokens, syntax_child::token(end - 1),
                                         preprocessing_op_or_punc::ELLIPSIS))
                    end--;
                if (!expansion_count(first, end, times))
                    return not_supported(id, "temp.variadic");
            }
            for (uint32_t k = 0; k < times; k++) {
                uint32_t position = static_cast<uint32_t>(arguments.size());
                if (position > pack)
                    position = pack;
                /* too many: clang's "too many template arguments" */
                if (position >= count)
                    return not_supported(id, "temp.arg");
                template_argument v;
                bool saved = expanding;
                uint32_t saved_element = expansion_element;
                if (expansion[w]) {
                    expanding = true;
                    expansion_element = k;
                    type_id_expansion = true;
                }
                analysis_result r = template_argument_for(id, ct, position, a,
                                                          arguments.data(), v);
                type_id_expansion = false;
                expanding = saved;
                expansion_element = saved_element;
                if (r != analysis_result::well_formed)
                    return r;
                if (position == pack)
                    elements.append(v);
                else
                    arguments.append(v);
            }
        }
    }
    /* [temp.arg]/4: the parameters after those given take their default
       template-arguments; one without is clang's "too few template
       arguments" */
    owned_array<bool> set;
    for (uint32_t i = 0; i < count; i++)
        set.append(i < arguments.size() || i == pack);
    while (arguments.size() < count)
        arguments.append(template_argument());
    if (pack < count)
        arguments[pack] =
            make_pack(elements.data(), static_cast<uint32_t>(elements.size()));
    /* a default the arguments make ill-formed: clang reports it at the
       default, "in instantiation of default argument" the use; neither
       declaration is judged */
    analysis_result r = default_template_arguments(id, ct, arguments, set);
    if (r == analysis_result::ill_formed)
        return not_supported(id, "temp.arg");
    return r;
}

bool semantic_analysis::same_template_parameters(uint32_t first,
                                                 uint32_t other_first,
                                                 uint32_t count) const
{
    for (uint32_t i = 0; i < count; i++) {
        const entity &a = entity_table[template_parameter_entities[first + i]];
        const entity &b =
            entity_table[template_parameter_entities[other_first + i]];
        if (a.kind != b.kind || template_parameter_is_pack(first + i) !=
                                    template_parameter_is_pack(other_first + i))
            return false;
        if (a.kind == entity_kind::none && !(a.type == b.type))
            return false;
    }
    return true;
}

/*
 * [temp.class]/1 with [dcl.type.elab]/2: template<class T> struct S; (a
 * class-key and an identifier).  The first declares the template, whose
 * definition comes later (class_template_definition completes its
 * record); a specialization named before then is an incomplete class
 * (instantiate_class).  Another declaration of the same template-head
 * declares it again ([temp.over.link]/6's equivalent heads; default
 * template-arguments merged across declarations are only the
 * definition's, [temp.param]/10).
 */
analysis_result
semantic_analysis::class_template_declaration(node_index n,
                                              node_index elaborated)
{
    entity_index outer = current_scope;
    const syntax_node &e = tree[elaborated];
    const syntax_child *parts = tree.children(elaborated);
    if (e.child_count != 2 || !parts[0].is_token() || !parts[1].is_token() ||
        tokens[parts[1].index()].kind != token_kind::identifier)
        return not_supported(n, "temp.class");
    standard_keyword key = p.keyword_of(tokens[parts[0].index()]);
    if (key != standard_keyword::class_ && key != standard_keyword::struct_)
        return not_supported(n, "temp.class");
    owned_array<node_index> parameters;
    analysis_result r = template_head_parameters(n, parameters);
    if (r != analysis_result::well_formed)
        return r;
    uint32_t name = tokens.identifier(parts[1].index());
    uint32_t index = static_cast<uint32_t>(function_templates.size());
    entity_index scope = entity_table.make(entity_kind::block_scope, 0, outer);
    uint32_t first = static_cast<uint32_t>(template_parameter_entities.size());
    r = declare_template_parameters(n, parameters, scope, index, name);
    if (r != analysis_result::well_formed)
        return r;
    entity_index held = entity_table.find(outer, name);
    if (held) {
        /* a redeclaration of the template, with no default
           template-argument (/12 forbids one given twice; merging is
           not analysed) */
        uint32_t ct = class_template_of(held);
        if (!ct || function_templates[ct - 1].name != held ||
            function_templates[ct - 1].primary)
            return not_supported(n, "temp.class");
        const function_template d = function_templates[ct - 1];
        if (entity_table[d.entity].enclosing != outer ||
            d.parameter_count != parameters.size() ||
            !same_template_parameters(first, d.first_parameter,
                                      d.parameter_count))
            return not_supported(n, "temp.class");
        for (node_index x : parameters)
            if (tree[x].child_count == 4)
                return not_supported(n, "temp.param");
        entity_table.add_declaration(d.entity, n);
        return analysis_result::well_formed;
    }
    /* [temp.param]/14 (a later declaration may not supply the defaults
       before) */
    for (size_t i = 1; i < parameters.size(); i++)
        if (tree[parameters[i - 1]].child_count == 4 &&
            tree[parameters[i]].child_count != 4)
            return not_supported(n, "temp.param");
    entity_index t =
        entity_table.make(entity_kind::template_entity, name, outer);
    entity_table[t].instantiation = index + 1;
    entity_table[t].linkage_of_name = linkage::external;
    if (within_unnamed_namespace(outer))
        entity_table[t].linkage_of_name = linkage::internal;
    entity_table.add_declaration(t, n);
    function_template record;
    record.entity = t;
    record.declaration = n;
    record.first_parameter = first;
    record.parameter_count = static_cast<uint32_t>(parameters.size());
    record.is_class = true;
    held = entity_table.make(entity_kind::none, name, outer);
    entity_table[held].held_by_template = true;
    entity_table.bind(outer, name, held);
    record.name = held;
    function_templates.append(record);
    return analysis_result::well_formed;
}

/* a function-definition's declarator-id S<A...>::f (behind ptr-operators):
   the simple-template-id, else 0 */
node_index semantic_analysis::out_of_class_member(node_index definition,
                                                  token_index *name) const
{
    const syntax_node &d = tree[definition];
    if (d.child_count != 3 || tree.children(definition)[1].is_token())
        return 0;
    node_index x = tree.children(definition)[1].index();
    while (tree[x].kind == syntax_kind::ptr_declarator) {
        const syntax_node &pd = tree[x];
        if (pd.child_count != 2 || tree.children(x)[1].is_token())
            return 0;
        x = tree.children(x)[1].index();
    }
    if (tree[x].kind != syntax_kind::noptr_declarator || !tree[x].child_count ||
        !syntax_is(tree, tree.children(x)[0], syntax_kind::qualified_id))
        return 0;
    node_index q = tree.children(x)[0].index();
    if (tree[q].child_count != 2 ||
        !syntax_is(tree, tree.children(q)[0],
                   syntax_kind::nested_name_specifier) ||
        !tree.children(q)[1].is_token() ||
        tokens[tree.children(q)[1].index()].kind != token_kind::identifier)
        return 0;
    node_index nested = tree.children(q)[0].index();
    if (name)
        *name = tree.children(q)[1].index();
    if (tree[nested].child_count != 2 ||
        !syntax_is(tree, tree.children(nested)[0],
                   syntax_kind::simple_template_id))
        return 0;
    return tree.children(nested)[0].index();
}

/*
 * [temp.mem.func]/1: "A member function of a class template may be
 * defined outside of the class template definition in which it is
 * declared."  /1's "the template-arguments are the same as the
 * template-parameters" of its class: template<class T> R S<T>::f(P...)
 * {...}, or of a partial specialization's ([temp.class.spec.mfunc]/1:
 * "The template parameter list of a member of a class template partial
 * specialization shall match the template parameter list of the class
 * template partial specialization.  The template argument list of a member
 * of a class template partial specialization shall match the template
 * argument list of the class template partial specialization."), with the
 * class's parameter names (its body finds them in the class).  Analysed
 * for each specialization of the class there is or will be
 * (define_template_member).
 */
analysis_result
semantic_analysis::member_definition_of_template(node_index n,
                                                 node_index definition)
{
    entity_index outer = current_scope;
    owned_array<node_index> parameters;
    analysis_result r = template_head_parameters(n, parameters);
    if (r != analysis_result::well_formed)
        return r;
    if (leading_gnu_attributes(definition) ||
        !syntax_is(tree, tree.children(definition)[0],
                   syntax_kind::decl_specifier_seq) ||
        !syntax_is(tree, tree.children(definition)[2],
                   syntax_kind::function_body))
        return not_supported(n, "temp.mem.func");
    /* [temp.param]/12: "A default template-argument shall not be specified
       in the template-parameter-lists of the definition of a member of a
       class template that appears outside of the member's class." */
    for (node_index x : parameters)
        if (tree[x].child_count == 4)
            return not_supported(x, "temp.param");
    node_index id = out_of_class_member(definition);
    const syntax_child *parts = tree.children(id);
    if (tree[id].child_count < 3 || !parts[0].is_token() ||
        tokens[parts[0].index()].kind != token_kind::identifier)
        return not_supported(n, "temp.mem.func");
    lookup_result x = lookup_unqualified(tokens.identifier(parts[0].index()),
                                         lookup_filter::any);
    entity_index named = x.entity;
    if (!named)
        named = x.unknown_entity;
    uint32_t ct = class_template_of(named);
    if (!ct || function_templates[ct - 1].name != named ||
        function_templates[ct - 1].primary ||
        !function_templates[ct - 1].definition ||
        entity_table[function_templates[ct - 1].entity].enclosing != outer)
        return not_supported(n, "temp.mem.func");
    uint32_t index = static_cast<uint32_t>(function_templates.size());
    entity_index scope = entity_table.make(entity_kind::block_scope, 0, outer);
    uint32_t first = static_cast<uint32_t>(template_parameter_entities.size());
    r = declare_template_parameters(n, parameters, scope, index, 0);
    if (r != analysis_result::well_formed)
        return r;
    /* the template-id's arguments in terms of the parameters; what they
       record for claims is the template's */
    size_t expressions_before = expressions.size();
    size_t uses_before = name_uses.size();
    owned_array<template_argument> arguments;
    current_scope = scope;
    r = template_id_arguments(id, ct, arguments);
    current_scope = outer;
    expressions.resize(expressions_before);
    name_uses.resize(uses_before);
    if (r != analysis_result::well_formed)
        return r;
    uint32_t pattern = 0;
    owned_array<template_argument> deduced;
    r = matching_specialization(id, ct, arguments.data(),
                                static_cast<uint32_t>(arguments.size()),
                                pattern, deduced);
    if (r != analysis_result::well_formed)
        return r;
    const template_argument *mine = arguments.data();
    uint32_t mine_count = static_cast<uint32_t>(arguments.size());
    function_template of = function_templates[ct - 1];
    if (pattern) {
        of = function_templates[pattern - 1];
        mine = deduced.data();
        mine_count = static_cast<uint32_t>(deduced.size());
    }
    /* each argument the parameter of the same position, with the
       class's parameter's name */
    if (!of.parameter_count || mine_count != parameters.size() ||
        of.parameter_count != parameters.size() ||
        !same_template_parameters(first, of.first_parameter,
                                  of.parameter_count))
        return not_supported(n, "temp.mem.func");
    for (uint32_t i = 0; i < mine_count; i++) {
        entity_index pe = template_parameter_entities[first + i];
        const template_argument &a = mine[i];
        bool same = a.template_entity == pe ||
                    (a.kind == template_argument::kind_t::type &&
                     !a.template_entity && a.type == entity_table[pe].type);
        if (!same ||
            entity_table[pe].name !=
                entity_table[template_parameter_entities[of.first_parameter +
                                                         i]]
                    .name)
            return not_supported(n, "temp.mem.func");
    }
    template_member_definition record;
    record.declaration = n;
    record.definition = definition;
    record.template_entity = function_templates[ct - 1].entity;
    record.pattern = pattern;
    uint32_t member = static_cast<uint32_t>(template_member_definitions.size());
    template_member_definitions.append(record);
    /* the specializations of the class defined before this */
    analysis_result result = analysis_result::well_formed;
    for (uint32_t k = 0; k < instantiations.size(); k++) {
        const instantiation_record &y = instantiations[k];
        if (y.template_entity != record.template_entity ||
            y.pattern != pattern || !y.specialization || y.defining ||
            entity_table[y.specialization].kind != entity_kind::class_type ||
            !entity_table[y.specialization].defined)
            continue;
        analysis_result one = define_template_member(member, k, false);
        if (one != analysis_result::well_formed &&
            result == analysis_result::well_formed)
            result = one;
    }
    return result;
}

/*
 * [temp.inst]/4: "the implicit instantiation of a class template
 * specialization causes ... the implicit instantiation of the definitions
 * of ... member functions ... when used"; here with the class (as its
 * members defined in it).  The function-definition analysed again where
 * the class's template-parameters' names are its arguments: its
 * nested-name-specifier names the specialization, whose member it defines
 * ([class.mfct]/1), inline as the class's own member functions are
 * ([basic.def.odr]/13: emitted where used, weak).  Errors are the
 * instantiation's, not judged here ([temp.inst]/1; clang reports them at
 * the definition "in instantiation of" the use): unsupported.
 */
analysis_result semantic_analysis::define_template_member(uint32_t member,
                                                          uint32_t index,
                                                          bool recorded)
{
    const template_member_definition o = template_member_definitions[member];
    const instantiation_record x = instantiations[index];
    if (instantiation_depth >= 64)
        return not_supported(o.declaration, "temp.inst");
    function_template of =
        function_templates[entity_table[x.template_entity].instantiation - 1];
    uint32_t first = x.first_argument;
    if (x.pattern) {
        of = function_templates[x.pattern - 1];
        first = x.first_deduced;
    }
    entity_index scope = entity_table.make(
        entity_kind::block_scope, 0, entity_table[x.template_entity].enclosing);
    for (uint32_t i = 0; i < of.parameter_count; i++)
        bind_template_argument(
            scope, template_parameter_entities[of.first_parameter + i],
            template_arguments[first + i]);
    entity_index saved_scope = current_scope;
    entity_index saved_class = defining_class;
    uint8_t saved_access = declaration_access;
    entity_index saved_last = last_declared;
    node_index saved_declaration = current_declaration;
    uint32_t saved_cv = function_cv;
    bool saved_condition = in_condition;
    token_index saved_callee = overloaded_callee;
    bool saved_claimable = declarations_claimable;
    uint32_t saved_instantiating = instantiating;
    uint32_t saved_class_instantiating = class_instantiating;
    token_index saved_horizon = lookup_horizon;
    unsigned saved_unevaluated = unevaluated;
    function_context saved_function =
        static_cast<function_context &&>(function_state);
    function_state = function_context();
    owned_array<parameter_meaning> saved_parameters =
        static_cast<owned_array<parameter_meaning> &&>(function_parameters);
    size_t expressions_before = expressions.size();
    size_t uses_before = name_uses.size();
    entity_index saved_member_scope = template_member_scope;
    template_member_scope = scope;
    current_scope = scope;
    current_declaration = o.declaration;
    defining_class = 0;
    declaration_access = 0;
    in_condition = false;
    overloaded_callee = 0;
    declarations_claimable = false;
    unevaluated = 0;
    instantiating = 0;
    class_instantiating = 0;
    lookup_horizon = tree[o.declaration].first_token;
    instantiation_depth++;
    analysis_result r = function_definition(o.definition);
    instantiation_depth--;
    template_member_scope = saved_member_scope;
    class_instantiating = saved_class_instantiating;
    unevaluated = saved_unevaluated;
    lookup_horizon = saved_horizon;
    instantiating = saved_instantiating;
    declarations_claimable = saved_claimable;
    overloaded_callee = saved_callee;
    in_condition = saved_condition;
    function_cv = saved_cv;
    last_declared = saved_last;
    current_declaration = saved_declaration;
    declaration_access = saved_access;
    defining_class = saved_class;
    current_scope = saved_scope;
    function_state = static_cast<function_context &&>(saved_function);
    function_parameters =
        static_cast<owned_array<parameter_meaning> &&>(saved_parameters);
    expressions.resize(expressions_before);
    name_uses.resize(uses_before);
    /* the member defined: the class's function this definition declares
       (last_declared may be a name its body declared) */
    entity_index defined = 0;
    if (r == analysis_result::well_formed) {
        token_index name = 0;
        out_of_class_member(o.definition, &name);
        entity_index f =
            entity_table.find(x.specialization, tokens.identifier(name));
        if (f && entity_table[f].kind == entity_kind::function &&
            (declared_by(f, o.definition) || declared_by(f, o.declaration)))
            defined = f;
        if (!defined)
            r = analysis_result::unsupported;
    }
    if (r == analysis_result::well_formed) {
        entity_table[defined].inline_ = true;
        return r;
    }
    size_t unsupported_before = unsupported.size();
    not_supported(o.declaration, "temp.inst");
    if (!recorded)
        return analysis_result::unsupported;
    for (analysed_declaration &d : declarations) {
        if (d.node != o.declaration || d.result != analysis_result::well_formed)
            continue;
        d.result = analysis_result::unsupported;
        d.first_unsupported = static_cast<uint32_t>(unsupported_before);
    }
    return analysis_result::unsupported;
}

} // namespace cplusplus
