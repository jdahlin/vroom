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
    for (node_index p : parameters) {
        const syntax_node &x = tree[p];
        const syntax_child *c = tree.children(p);
        bool type =
            syntax_is(tree, syntax_child::node(p), syntax_kind::type_parameter);
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

static bool depends(const types &table, qualified_type t,
                    const entities *parameters = nullptr, uint64_t from = 0);

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
        token_index id = tree.children(node)[1].index();
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
    if (tree[head].child_count >= 2 &&
        syntax_is(tree, tree.children(head)[1],
                  syntax_kind::simple_template_id))
        return class_specialization_definition(n, specifier, parameters);
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
            tree[parameters[i]].child_count != 4)
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
    for (const template_argument &a : arguments)
        if (depends(type_table, a.type) || a.template_entity)
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
    owned_array<template_argument> values;
    owned_array<bool> set;
    for (uint32_t i = 0; i < ft.parameter_count; i++) {
        template_argument v;
        if (i < specified)
            v = deduced[i];
        values.append(v);
        set.append(i < specified);
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
        if (!deduce_type(p, a, values, set))
            return not_supported(at, "temp.deduct.call");
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
    uint32_t count = function_templates[ct - 1].parameter_count;
    for (uint32_t i = 2; i + 1 < tree[id].child_count; i++) {
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
            uint32_t position = static_cast<uint32_t>(arguments.size());
            /* too many: clang's "too many template arguments" */
            if (position >= count)
                return not_supported(id, "temp.arg");
            template_argument v;
            analysis_result r =
                template_argument_for(id, ct, position, a, arguments.data(), v);
            if (r != analysis_result::well_formed)
                return r;
            arguments.append(v);
        }
    }
    /* [temp.arg]/4: the parameters after those given take their default
       template-arguments; one without is clang's "too few template
       arguments" */
    owned_array<bool> set;
    for (uint32_t i = 0; i < count; i++)
        set.append(i < arguments.size());
    while (arguments.size() < count)
        arguments.append(template_argument());
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
        if (a.kind != b.kind)
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
