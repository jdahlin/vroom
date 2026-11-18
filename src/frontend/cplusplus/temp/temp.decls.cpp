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
            }
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
    return not_supported(n, "temp.expl.spec");
}

/* [temp.explicit]/1: "A class, function, variable, or member template
   specialization can be explicitly instantiated from its template." */
analysis_result semantic_analysis::explicit_instantiation(node_index n)
{
    return not_supported(n, "temp.explicit");
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
analysis_result
semantic_analysis::template_head_parameters(node_index n,
                                            owned_array<node_index> &parameters)
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
        if (syntax_is(tree, c, syntax_kind::type_parameter)) {
            parameters.append(c.index());
            continue;
        }
        if (!syntax_is(tree, c, syntax_kind::template_parameter_list))
            return not_supported(n, "temp.constr");
        for (uint32_t k = 0; k < tree[c.index()].child_count; k++) {
            syntax_child p = tree.children(c.index())[k];
            if (p.is_token())
                continue;
            if (!syntax_is(tree, p, syntax_kind::type_parameter))
                return not_supported(p.index(), "temp.param");
            parameters.append(p.index());
        }
    }
    /* a type-parameter of the form class identifier: a default, a pack
       or a type-constraint is a rule of its own */
    for (node_index p : parameters) {
        const syntax_node &x = tree[p];
        if (x.child_count != 2 || !tree.children(p)[0].is_token() ||
            !tree.children(p)[1].is_token() ||
            tokens[tree.children(p)[1].index()].kind != token_kind::identifier)
            return not_supported(p, "temp.param");
    }
    if (parameters.empty())
        return not_supported(n, "temp.param");
    return analysis_result::well_formed;
}

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
    analysis_result head = template_head_parameters(n, parameters);
    if (head != analysis_result::well_formed)
        return head;
    const syntax_node &d = tree[definition];
    if (leading_gnu_attributes(definition) || d.child_count != 3 ||
        !syntax_is(tree, tree.children(definition)[0],
                   syntax_kind::decl_specifier_seq) ||
        tree.children(definition)[1].is_token() ||
        !syntax_is(tree, tree.children(definition)[2],
                   syntax_kind::function_body))
        return not_supported(n, "temp.fct");
    uint32_t index = static_cast<uint32_t>(function_templates.size());
    /* the scope of the template-parameters ([basic.scope.temp]) */
    entity_index scope = entity_table.make(entity_kind::block_scope, 0, outer);
    uint32_t first = static_cast<uint32_t>(template_parameter_entities.size());
    for (size_t i = 0; i < parameters.size(); i++) {
        token_index id = tree.children(parameters[i])[1].index();
        uint32_t name = tokens.identifier(id);
        /* [temp.local]/6: "The name of a template-parameter shall not be
           redeclared within its scope (including nested scopes)." */
        if (entity_table.find(scope, name))
            return ill_formed_at_token(id,
                                       "the name of a template-parameter "
                                       "shall not be redeclared within its "
                                       "scope",
                                       "temp.local");
        entity_index e =
            entity_table.make(entity_kind::typedef_name, name, scope);
        entity_table[e].type = qualified_type::make(
            type_table.of_entity(type_kind::template_parameter, e), cv_none);
        entity_table[e].value = i;
        entity_table[e].defined = true;
        entity_table[e].instantiation = index + 1;
        entity_table.bind(scope, name, e);
        template_parameter_entities.append(e);
    }
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
    uint32_t name = tokens.identifier(m.id);
    entity_index t =
        entity_table.make(entity_kind::template_entity, name, outer);
    entity_table[t].type = type;
    entity_table[t].defined = true;
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
    record.definition = definition;
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
                    const entities *parameters = nullptr, uint64_t from = 0)
{
    const struct type &r = table[t.unqualified()];
    switch (r.kind) {
    case type_kind::template_parameter:
        return !parameters || (*parameters)[r.entity].value >= from;
    case type_kind::dependent:
        return true;
    case type_kind::pointer:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
    case type_kind::array:
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
    analysis_result r = template_head_parameters(n, parameters);
    if (r != analysis_result::well_formed)
        return r;
    /* class-head: class-key identifier base-clause_opt */
    const syntax_node &spec = tree[specifier];
    if (!spec.child_count ||
        !syntax_is(tree, tree.children(specifier)[0], syntax_kind::class_head))
        return not_supported(n, "temp.class");
    node_index head = tree.children(specifier)[0].index();
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
    for (size_t i = 0; i < parameters.size(); i++) {
        token_index id = tree.children(parameters[i])[1].index();
        uint32_t p = tokens.identifier(id);
        /* [temp.local]/6 */
        if (entity_table.find(scope, p) || p == name)
            return ill_formed_at_token(id,
                                       "the name of a template-parameter "
                                       "shall not be redeclared within its "
                                       "scope",
                                       "temp.local");
        entity_index e = entity_table.make(entity_kind::typedef_name, p, scope);
        entity_table[e].type = qualified_type::make(
            type_table.of_entity(type_kind::template_parameter, e), cv_none);
        entity_table[e].value = i;
        entity_table[e].defined = true;
        entity_table[e].instantiation = index + 1;
        entity_table.bind(scope, p, e);
        template_parameter_entities.append(e);
    }
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
    for (uint32_t i = 2; i + 1 < t.child_count; i++) {
        owned_array<syntax_child> written;
        if (syntax_is(tree, parts[i], syntax_kind::template_argument_list)) {
            for (uint32_t k = 0; k < tree[parts[i].index()].child_count; k++) {
                syntax_child a = tree.children(parts[i].index())[k];
                if (!syntax_is_punctuator(tokens, a,
                                          preprocessing_op_or_punc::COMMA))
                    written.append(a);
            }
        } else {
            written.append(parts[i]);
        }
        for (syntax_child a : written) {
            if (!syntax_is(tree, a, syntax_kind::type_id))
                return not_supported(id, "temp.arg");
            template_argument v;
            analysis_result r = type_id(a.index(), v.type);
            if (r != analysis_result::well_formed)
                return r;
            arguments.append(v);
        }
    }
    /* [temp.arg]/4 (no default template-arguments): one argument per
       parameter, else clang's "too few/many template arguments" */
    if (arguments.size() != function_templates[ct - 1].parameter_count)
        return not_supported(id, "temp.arg");
    bool dependent = false;
    for (const template_argument &a : arguments)
        if (depends(type_table, a.type))
            dependent = true;
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
                same = template_arguments[x.first_argument + i].type ==
                       arguments[i].type;
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
                                    owned_array<qualified_type> &deduced) const
{
    const struct type &pr = type_table[p.unqualified()];
    if (pr.kind == type_kind::template_parameter) {
        /* (P's cv-qualifiers beyond A's: [temp.deduct.call]/4.1's "more
           cv-qualified" deduced A, or 4.2's qualification conversion; the
           call's conversions check the rest) */
        qualified_type value =
            qualified_type::make(a.unqualified(), a.cv() & ~p.cv());
        uint64_t i = entity_table[pr.entity].value;
        if (i >= deduced.size())
            return false;
        if (deduced[i].unqualified() && !(deduced[i] == value))
            return false;
        deduced[i] = value;
        return true;
    }
    if (!depends(type_table, p))
        return p == a;
    if (pr.kind == type_kind::dependent) {
        /* [temp.deduct.type]/8's TT<T>: a class template's specialization,
           whose arguments are deduced from those of A's, a
           specialization of the same template */
        const struct type &ar = type_table[a.unqualified()];
        if (ar.kind != type_kind::class_type || p.cv() != a.cv() ||
            !entity_table[ar.entity].instantiation ||
            specialized_template(ar.entity) != specialized_template(pr.entity))
            return false;
        uint32_t count = specialization_argument_count(pr.entity);
        for (uint32_t i = 0; i < count; i++)
            if (!deduce_type(specialization_argument(pr.entity, i),
                             specialization_argument(ar.entity, i), deduced))
                return false;
        return true;
    }
    const struct type &ar = type_table[a.unqualified()];
    if (pr.kind != ar.kind || p.cv() != a.cv())
        return false;
    switch (pr.kind) {
    case type_kind::pointer:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
        return deduce_type(pr.operand, ar.operand, deduced);
    case type_kind::array:
        if (pr.bound_known != ar.bound_known || pr.bound != ar.bound)
            return false;
        return deduce_type(pr.operand, ar.operand, deduced);
    case type_kind::function: {
        if (pr.variadic != ar.variadic ||
            pr.parameter_count != ar.parameter_count)
            return false;
        if (!deduce_type(pr.operand, ar.operand, deduced))
            return false;
        const qualified_type *pp = type_table.parameters(p.unqualified());
        const qualified_type *ap = type_table.parameters(a.unqualified());
        for (uint32_t i = 0; i < pr.parameter_count; i++)
            if (!deduce_type(pp[i], ap[i], deduced))
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
    if (f.variadic || count != f.parameter_count)
        return not_supported(at, "temp.deduct.call");
    const qualified_type *parameters =
        type_table.parameters(type.unqualified());
    /* [temp.arg.explicit]/3: "A template argument list may be specified
       when referring to a specialization of a function template"; /6:
       "The template argument list shall not specify more
       template-arguments than there are corresponding
       template-parameters" (clang: no candidate) */
    uint32_t specified = static_cast<uint32_t>(deduced.size());
    if (specified > ft.parameter_count)
        return not_supported(at, "temp.arg.explicit");
    owned_array<qualified_type> values;
    for (uint32_t i = 0; i < ft.parameter_count; i++) {
        qualified_type v;
        if (i < specified)
            v = deduced[i].type;
        values.append(v);
    }
    for (uint32_t i = 0; i < count; i++) {
        qualified_type p = parameters[i];
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
        if (!deduce_type(p, a, values))
            return not_supported(at, "temp.deduct.call");
    }
    deduced.clear();
    for (const qualified_type &v : values) {
        /* [temp.deduct]/1's "all of the template arguments shall have
           values": one deduced from no argument */
        if (!v.unqualified())
            return not_supported(at, "temp.deduct.call");
        template_argument x;
        x.type = v;
        deduced.append(x);
    }
    return analysis_result::well_formed;
}

} // namespace cplusplus
