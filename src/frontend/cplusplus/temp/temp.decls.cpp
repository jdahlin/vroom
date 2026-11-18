/*
 * temp.decls.cpp: the semantic side of [temp.pre] and [temp.deduct.guide]:
 * the declarations of templates (function, class, alias, variable) and of
 * class templates' partial specializations and members.  Explicit
 * specializations and instantiations are in temp.explicit.cpp, template
 * arguments in temp.arg.cpp, deduction in temp.deduct.cpp.  (The grammar
 * is the parser's, in temp.pre.cpp and temp.concept.cpp.)
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
        return concept_definition(n, inner);
    /* [temp.alias]/1: "A template-declaration in which the declaration is
       an alias-declaration declares the identifier to be an alias
       template." */
    case syntax_kind::alias_declaration:
        return alias_template_definition(n, inner);
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
            node_index only = sole_decl_specifier(inner);
            if (only && tree[only].kind == syntax_kind::class_specifier)
                return class_template_definition(n, only);
            /* [temp.class]/1's declaration without a definition
               (template<class T> struct S;) */
            if (only &&
                tree[only].kind == syntax_kind::elaborated_type_specifier)
                return class_template_declaration(n, only);
            /* [temp.pre]/1: a variable template, its one init-declarator
               with an initializer, after attributes without effect
               (libc++'s deprecated is_pod_v) */
            uint32_t lead = attributes_without_effect_before(inner);
            if (d.child_count == 3 + lead &&
                syntax_is(tree, tree.children(inner)[lead],
                          syntax_kind::decl_specifier_seq) &&
                syntax_is(tree, tree.children(inner)[lead + 1],
                          syntax_kind::init_declarator)) {
                /* (a specialization's template-id after attributes: no
                   rules yet) */
                node_index item = tree.children(inner)[lead + 1].index();
                if (lead && tree[item].child_count &&
                    syntax_is(tree, tree.children(item)[0],
                              syntax_kind::simple_template_id))
                    return not_supported(n, "temp.pre");
                if (node_index id = variable_specialization_id(inner))
                    return variable_partial_specialization(n, inner, id);
                return variable_template_definition(n, inner);
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
        /* a function template's declaration or a variable template of
           several declarators: which needs the declarator's type */
        return not_supported(n, "temp.pre");
    }
    default:
        return not_supported(n, "temp.pre");
    }
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
/* [temp.pre]/1: a template-head's requires-clause's constraint-expression,
   or none */
syntax_child semantic_analysis::head_constraint(node_index n) const
{
    if (!tree[n].child_count ||
        !syntax_is(tree, tree.children(n)[0], syntax_kind::template_head))
        return syntax_child();
    node_index head = tree.children(n)[0].index();
    for (uint32_t i = 0; i < tree[head].child_count; i++) {
        syntax_child c = tree.children(head)[i];
        if (syntax_is(tree, c, syntax_kind::requires_clause) &&
            tree[c.index()].child_count == 2)
            return tree.children(c.index())[1];
    }
    return syntax_child();
}

analysis_result semantic_analysis::template_head_parameters(
    node_index n, owned_array<node_index> &parameters, bool packs,
    bool constrained, bool pack_anywhere)
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
    if (!template_head_list(head, parameters))
        return not_supported(n, "temp.pre");
    if (head_constraint(n).bits && !constrained)
        return not_supported(n, "temp.constr");
    /* a type-parameter class identifier_opt, a template template-parameter
       template < type-parameters > class identifier_opt, a
       parameter-declaration decl-specifier-seq identifier_opt, each with =
       and a default template-argument or not: a type-constraint or another
       declarator is a rule of its own */
    for (size_t i = 0; i < parameters.size(); i++) {
        node_index p = parameters[i];
        template_parameter_parts x;
        if (!template_parameter_form(p, x))
            return not_supported(p, "temp.param");
        /* [temp.param]/18: "If a template-parameter is a type-parameter
           with an ellipsis prior to its optional identifier ... then the
           template-parameter is a template parameter pack"; /17 a
           parameter-declaration's, int... N: the template's last
           parameter */
        if (x.pack) {
            /* [temp.param]/14: "If a template-parameter of a primary class
               template ... is a template parameter pack, it shall be the
               last template-parameter.  A template parameter pack of a
               function template shall not be followed by another
               template parameter unless that template parameter can be
               deduced": a partial specialization's are all deduced
               ([temp.class.spec]/8.3 aside) */
            if (!packs || (i + 1 != parameters.size() && !pack_anywhere) ||
                x.template_template || x.has_default)
                return not_supported(p, "temp.variadic");
            continue;
        }
        if (!x.template_template)
            continue;
        /* [temp.param]/1's template-head type-parameter-key: its own
           parameters type-parameters or integral parameter-declarations,
           without defaults; a pack its last */
        owned_array<node_index> inner;
        if (!template_head_list(x.head, inner) || inner.empty())
            return not_supported(p, "temp.arg.template");
        for (size_t k = 0; k < inner.size(); k++) {
            template_parameter_parts y;
            if (!template_parameter_form(inner[k], y) || y.has_declarator ||
                y.template_template || y.has_default ||
                (y.pack && (!y.type || k + 1 != inner.size())))
                return not_supported(p, "temp.arg.template");
        }
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
    /* [temp.param]/14: a function template's pack may be followed by
       parameters that are deduced or have defaults */
    analysis_result head =
        template_head_parameters(n, parameters, true, true, true);
    if (head != analysis_result::well_formed)
        return head;
    const syntax_node &d = tree[definition];
    /* a function-definition, or a simple-declaration of one declarator
       without an initializer: the template declared, defined later;
       [ext:gnu_attributes] before it, its specializations' */
    bool defines = d.kind == syntax_kind::function_definition;
    uint32_t lead = leading_gnu_attributes(definition);
    const syntax_child *dc = tree.children(definition) + lead;
    if (d.child_count != lead + 3 ||
        !syntax_is(tree, dc[0], syntax_kind::decl_specifier_seq) ||
        dc[1].is_token() ||
        (defines && !syntax_is(tree, dc[2], syntax_kind::function_body)) ||
        (!defines && (!syntax_is_punctuator(
                          tokens, dc[2], preprocessing_op_or_punc::SEMICOLON) ||
                      (!syntax_is(tree, dc[1], syntax_kind::noptr_declarator) &&
                       !syntax_is(tree, dc[1], syntax_kind::ptr_declarator)))))
        return not_supported(n, "temp.fct");
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    analysis_result declared =
        template_parameter_scope(n, parameters, outer, 0, index, scope, first);
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
    analysis_result r = decl_specifier_seq(dc[0].index(), s, false);
    for (uint32_t i = 0; i < lead && r == analysis_result::well_formed; i++)
        r = gnu_attribute_specifier(tree.children(definition)[i].index(), s);
    if (r == analysis_result::well_formed) {
        type = s.type;
        function_cv = 0;
        function_parameters.clear();
        r = declarator(definition, dc[1], type, m);
    }
    current_scope = outer;
    expressions.resize(expressions_before);
    name_uses.resize(uses_before);
    if (r != analysis_result::well_formed)
        return r;
    /* an operator function template (libc++'s byte& operator<<=(byte&,
       _Integer)): its name is operator@ ([over.oper]); not new or delete */
    if (!m.has_id || m.qualifier || m.qualified_namespace ||
        m.allocation != allocation_function::none || m.literal_operator ||
        function_cv ||
        type_table[type.unqualified()].kind != type_kind::function)
        return not_supported(n, "temp.fct");
    /* [ext:gnu_attributes] its specializations' symbols' (given them at
       instantiation) */
    uint32_t gnu_flags = s.gnu_flags | m.gnu_flags;
    if (s.typedef_ || s.extern_ || c_language ||
        (gnu_flags & ~gnu_function_symbol_flags) || s.aligned || m.aligned ||
        s.section || m.section ||
        (s.abi_tag && m.abi_tag && s.abi_tag != m.abi_tag))
        return not_supported(n, "temp.fct");
    token_index abi_tag = m.abi_tag;
    if (!abi_tag)
        abi_tag = s.abi_tag;
    for (const parameter_meaning &p : function_parameters)
        if (p.default_argument.index())
            return not_supported(n, "dcl.fct.default");
    /* [temp.variadic]/7: "A pack whose name appears within the pattern of
       a pack expansion is expanded by that pack expansion"; a pack named
       outside one is ill-formed (/7), which the template's own declarator
       does not tell apart from a pattern analysis has no rules for: the
       last parameter's type the only pack expansion, a function parameter
       pack of the template parameter pack (its only one) */
    {
        uint32_t last = static_cast<uint32_t>(parameters.size()) - 1;
        uint32_t packs = 0;
        for (uint32_t i = 0; i < parameters.size(); i++)
            if (template_parameter_is_pack(first + i)) {
                last = i;
                packs++;
            }
        if (packs > 1)
            return not_supported(n, "temp.variadic");
        bool pack = packs == 1;
        const struct type ft = type_table[type.unqualified()];
        const qualified_type *list = type_table.parameters(type.unqualified());
        /* the return type: the pack named only in pack expansions
           (res<A...>), each name of it followed by ... */
        if (pack && depends(type_table, ft.operand, &entity_table, last) &&
            !pack_only_expanded(
                entity_table[template_parameter_entities[first + last]].name,
                tree[dc[0].index()].first_token, tree[dc[0].index()].end_token))
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
    owned_array<uint32_t> group;
    if (prior)
        function_template_overloads(prior, group);
    uint32_t earlier = 0;
    for (uint32_t g : group) {
        /* [temp.over.link]/7: "Two function templates are equivalent if
           they are declared in the same scope, have the same name, have
           equivalent template-heads, and have return types, parameter
           lists, and trailing requires-clauses (if any) that are
           equivalent using the rules described above to compare
           expressions involving template parameters"; one that is not is
           another template of the name, an overload ([temp.over]) */
        const function_template f = function_templates[g - 1];
        if (entity_table[f.entity].enclosing != outer)
            return not_supported(n, "temp.over.link");
        if (f.parameter_count != parameters.size() ||
            !same_template_parameters(first, f.first_parameter,
                                      f.parameter_count) ||
            !same_constraints(f.declaration, n))
            continue;
        owned_array<template_argument> values;
        owned_array<bool> set;
        for (uint32_t i = 0; i < f.parameter_count; i++) {
            values.append(template_argument());
            set.append(false);
        }
        bool same =
            deduce_type(entity_table[f.entity].type, type, values, set, true);
        for (uint32_t i = 0; i < f.parameter_count && same; i++) {
            entity_index pe = template_parameter_entities[first + i];
            if (set[i] && (entity_table[pe].kind != entity_kind::typedef_name ||
                           !(values[i].type == entity_table[pe].type)))
                same = false;
        }
        if (same)
            earlier = g;
    }
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
    function_template record = new_template(
        name, outer, n, index, first, static_cast<uint32_t>(parameters.size()));
    record.gnu_flags = gnu_flags;
    record.abi_tag = abi_tag;
    entity_index t = record.entity;
    entity_table[t].type = type;
    entity_table[t].defined = defines;
    entity_table[t].inline_ = s.inline_;
    entity_table[t].constexpr_ = s.constexpr_;
    /* [over.oper]: an operator function template's specializations are
       operator functions (named by the operator: [abi:mangling-operator]) */
    entity_table[t].operator_function = m.operator_function;
    entity_table[t].implemented = m.implemented;
    /* [basic.link]/3.1: a function template declared static has internal
       linkage */
    if (s.static_)
        entity_table[t].linkage_of_name = linkage::internal;
    if (defines)
        record.definition = definition;
    record.declarator = definition;
    /* the name: hold_names binds a fresh one; one it finds bound already
       declares something else too (an overload, another template), which
       calls of it need [over.match.funcs] for */
    /* the functions of the scope the name denotes ([over.pre]/1), which a
       call of the name weighs against the template's specializations
       ([temp.over]/1): the name is held by an entity of its own, the
       functions kept */
    owned_array<entity_index> functions;
    /* (a scope with a using-directive, an inline namespace's among them,
       or names not known: lookup of the name finds more than the
       scope's own declarations) */
    bool own = !entity_table[outer].has_using_directive &&
               !entity_table[outer].unknown_names;
    if (prior && group.empty() && own &&
        entity_table[prior].kind == entity_kind::function &&
        entity_table[prior].enclosing == outer &&
        !entity_table[prior].nonstatic_member &&
        !entity_table.bound_by_using(outer, name)) {
        overload_set(prior, functions);
        prior = entity_table.make(entity_kind::none, name, outer);
        entity_table[prior].held_by_template = true;
        entity_table.bind(outer, name, prior);
    }
    size_t before = entity_table.size();
    /* (another function template of the name, group, keeps it: the
       name's templates are overloaded, [temp.over]) */
    /* (an operator function template is followed: operator expressions
       find it, over.match.oper) */
    bool operators_were_held = operators_held;
    hold_names(n);
    operators_held = operators_were_held;
    entity_index held = entity_table.find(outer, name);
    if (!held && m.operator_function) {
        /* (hold_names holds identifiers: an operator's name is held here) */
        held = entity_table.make(entity_kind::none, name, outer);
        entity_table[held].held_by_template = true;
        entity_table.bind(outer, name, held);
    }
    record.name = held;
    bool overload = (!group.empty() || !functions.empty()) && own &&
                    held == prior &&
                    entity_table[held].kind == entity_kind::none;
    if (overload)
        for (entity_index f : functions) {
            template_name_function x;
            x.name = held;
            x.function = f;
            template_name_functions.append(x);
        }
    for (uint32_t g : group)
        function_templates[g - 1].unusable = !overload;
    if (!overload && (!held || held < before ||
                      entity_table[held].kind != entity_kind::none)) {
        record.unusable = true;
        for (function_template &other : function_templates)
            if (other.name == held)
                other.unusable = true;
    }
    function_templates.append(record);
    return analysis_result::well_formed;
}

/*
 * The position among the template's parameters of the type parameter whose
 * type t is (cv-qualified or not), when it is one of the parameters of this
 * template-parameter-list declared before (their entities from
 * template_parameter_entities[first] on, in scope); UINT32_MAX otherwise.
 */
uint32_t semantic_analysis::own_type_parameter_position(qualified_type t,
                                                        entity_index scope,
                                                        uint32_t first) const
{
    const struct type &record = type_table[t.unqualified()];
    if (record.kind != type_kind::template_parameter)
        return UINT32_MAX;
    entity_index q = record.entity;
    for (size_t k = first; k < template_parameter_entities.size(); k++)
        if (template_parameter_entities[k] == q &&
            entity_table[q].enclosing == scope)
            return static_cast<uint32_t>(entity_table[q].value);
    return UINT32_MAX;
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
    uint32_t &index, uint32_t reserved)
{
    owned_array<uint32_t> template_templates;
    uint32_t first = static_cast<uint32_t>(template_parameter_entities.size());
    for (size_t i = 0; i < parameters.size(); i++) {
        node_index node = parameters[i];
        template_parameter_parts form;
        if (!template_parameter_form(node, form))
            return not_supported(node, "temp.param");
        token_index id = form.id;
        uint32_t name = 0;
        if (form.named)
            name = tokens.identifier(id);
        /* [temp.local]/6: "The name of a template-parameter shall not be
           redeclared within its scope (including nested scopes)." */
        if (name && (entity_table.find(scope, name) || name == reserved))
            return ill_formed_at_token(id,
                                       "the name of a template-parameter "
                                       "shall not be redeclared within its "
                                       "scope",
                                       "temp.local");
        entity_index e = 0;
        if (form.template_template) {
            /* its record made below, after the template's parameters */
            e = entity_table.make(entity_kind::template_entity, name, scope);
            template_templates.append(static_cast<uint32_t>(i));
        } else if (form.type) {
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
            qualified_type type = sp.type;
            if (r == analysis_result::well_formed && form.has_declarator) {
                declarator_meaning dm;
                r = declarator(node, form.declarator, type, dm);
                if (r == analysis_result::well_formed &&
                    (dm.qualifier || dm.qualified_namespace || dm.pack ||
                     dm.gnu_flags || dm.aligned || dm.template_id ||
                     dm.has_id != form.named))
                    r = not_supported(node, "temp.param");
            }
            current_scope = saved;
            if (r != analysis_result::well_formed)
                return r;
            /* [temp.param]/4 with a type that depends on this template's
               parameters declared before it: template <class T, T v>
               (libc++'s integral_constant), whose argument gives its
               type, or typename enable_if<B, int>::type = 0 (libc++'s
               SFINAE), analysed again with the arguments
               (template_argument_for) */
            bool own_parameter_type =
                own_type_parameter_position(type, scope, first) != UINT32_MAX;
            if ((!own_parameter_type && !depends(type_table, type) &&
                 !structural_parameter_type(type)) ||
                sp.typedef_ || sp.static_ || sp.extern_ || sp.constexpr_ ||
                sp.inline_ || sp.gnu_flags)
                return not_supported(node, "temp.param");
            e = entity_table.make(entity_kind::none, name, scope);
            /* /6: "the top-level cv-qualifiers on the template-parameter
               are ignored when determining its type" */
            entity_table[e].type =
                qualified_type::make(type.unqualified(), cv_none);
        }
        entity_table[e].value = i;
        entity_table[e].defined = true;
        entity_table[e].instantiation = index + 1;
        if (name)
            entity_table.bind(scope, name, e);
        template_parameter_entities.append(e);
        template_parameter_nodes.append(node);
    }
    if (template_templates.empty())
        return analysis_result::well_formed;
    /* [temp.param]/1's template template-parameter: a class template of
       its own parameters (in a scope of their own), never defined; its
       records come before the template's, which moves to the end */
    for (uint32_t i : template_templates) {
        entity_index e = template_parameter_entities[first + i];
        template_parameter_parts form;
        template_parameter_form(parameters[i], form);
        owned_array<node_index> inner;
        template_head_list(form.head, inner);
        uint32_t own = static_cast<uint32_t>(function_templates.size());
        entity_index inner_scope =
            entity_table.make(entity_kind::block_scope, 0, scope);
        function_template record;
        record.entity = e;
        record.name = e;
        record.declaration = n;
        record.first_parameter =
            static_cast<uint32_t>(template_parameter_entities.size());
        record.parameter_count = static_cast<uint32_t>(inner.size());
        record.is_class = true;
        record.is_parameter = true;
        analysis_result r =
            declare_template_parameters(n, inner, inner_scope, own, 0);
        if (r != analysis_result::well_formed)
            return r;
        entity_table[e].instantiation = own + 1;
        function_templates.append(record);
    }
    index = static_cast<uint32_t>(function_templates.size());
    for (size_t i = 0; i < parameters.size(); i++) {
        entity_index e = template_parameter_entities[first + i];
        if (entity_table[e].kind != entity_kind::template_entity)
            entity_table[e].instantiation = index + 1;
    }
    return analysis_result::well_formed;
}

/* the usable function template whose name the held entity is, plus one */
uint32_t semantic_analysis::function_template_of(entity_index held) const
{
    for (size_t i = 0; i < function_templates.size(); i++)
        if (function_templates[i].name == held && held &&
            !function_templates[i].unusable &&
            !function_templates[i].is_class &&
            !function_templates[i].is_alias &&
            !function_templates[i].is_variable)
            return static_cast<uint32_t>(i + 1);
    return 0;
}

void semantic_analysis::function_template_overloads(
    entity_index held, owned_array<uint32_t> &out) const
{
    out.clear();
    for (size_t i = 0; i < function_templates.size(); i++)
        if (function_templates[i].name == held && held &&
            !function_templates[i].unusable &&
            !function_templates[i].is_class &&
            !function_templates[i].is_alias &&
            !function_templates[i].is_variable)
            out.append(static_cast<uint32_t>(i + 1));
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
    return template_of_form(e, &function_template::is_class);
}

uint32_t semantic_analysis::alias_template_of(entity_index e) const
{
    return template_of_form(e, &function_template::is_alias);
}

/* the usable template of the form (is_class, is_alias, is_variable) whose
   name the held entity e is, plus one */
uint32_t
semantic_analysis::template_of_form(entity_index e,
                                    bool function_template::*form) const
{
    if (!e)
        return 0;
    for (size_t i = 0; i < function_templates.size(); i++)
        if (function_templates[i].name == e && function_templates[i].*form &&
            !function_templates[i].unusable)
            return static_cast<uint32_t>(i + 1);
    return 0;
}

/* the simple-template-id id's template-name, an identifier, looked up
   unqualified (x), the entity it names or holds (named); false for another
   form */
bool semantic_analysis::template_id_name(node_index id, lookup_result &x,
                                         entity_index &named) const
{
    const syntax_child *parts = tree.children(id);
    if (tree[id].child_count < 3 || !parts[0].is_token() ||
        tokens[parts[0].index()].kind != token_kind::identifier)
        return false;
    x = lookup_unqualified(tokens.identifier(parts[0].index()),
                           lookup_filter::any);
    named = x.entity;
    if (!named)
        named = x.unknown_entity;
    return true;
}

/*
 * [temp.alias]/1: "A template-declaration in which the declaration is an
 * alias-declaration declares the identifier to be an alias template.  An
 * alias template is a name for a family of types."  One at namespace
 * scope, not declared before: its defining-type-id is analysed with the
 * template-parameters' types, then per template-id (alias_template_type).
 */
analysis_result semantic_analysis::alias_template_definition(node_index n,
                                                             node_index alias)
{
    entity_index outer = current_scope;
    owned_array<node_index> parameters;
    analysis_result r = template_head_parameters(n, parameters, true);
    if (r != analysis_result::well_formed)
        return r;
    uint32_t name = tokens.identifier(tree[alias].first_token + 1);
    if (entity_table.find(outer, name))
        return not_supported(n, "temp.alias");
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    r = template_parameter_scope(n, parameters, outer, name, index, scope,
                                 first);
    if (r != analysis_result::well_formed)
        return r;
    /* [temp.param]/14's rule is a class template's: a default after which
       one is missing is left to clang */
    if (default_missing_after_one(parameters))
        return not_supported(n, "temp.param");
    qualified_type pattern;
    entity_index saved_scope = current_scope;
    current_scope = scope;
    size_t unsupported_before = unsupported.size();
    r = alias_type(alias, pattern);
    current_scope = saved_scope;
    /* a type analysis cannot follow with the parameters unbound (T...
       where the template named takes a parameter before its pack,
       typename Q<T>::template X<U> of a dependent Q): it is analysed with
       each use's arguments (alias_template_type), as it is anyway, and
       judged there */
    if (r == analysis_result::unsupported) {
        unsupported.resize(unsupported_before);
        r = analysis_result::well_formed;
    }
    if (r != analysis_result::well_formed)
        return r;
    function_template record = new_template(
        name, outer, n, index, first, static_cast<uint32_t>(parameters.size()));
    entity_table[record.entity].defined = true;
    record.definition = alias;
    record.is_alias = true;
    hold_template_name(record, name, outer);
    return analysis_result::well_formed;
}

/*
 * [temp.class.spec]/1 (with [temp.pre]/1's
 * variable templates): template<class T> const bool v<T, void_t<T&>> =
 * true;: a partial specialization of a variable template, recorded as a
 * class template's is (class_specialization_definition): its arguments in
 * terms of its parameters, which instantiate_variable matches
 * (matching_specialization) and whose declaration it analyses with the
 * deduced arguments.
 */
analysis_result semantic_analysis::variable_partial_specialization(
    node_index n, node_index declaration, node_index id)
{
    entity_index outer = current_scope;
    owned_array<node_index> parameters;
    /* [temp.param]/14: a partial specialization's packs are deduced,
       wherever they are */
    analysis_result r =
        template_head_parameters(n, parameters, true, false, true);
    if (r != analysis_result::well_formed)
        return r;
    lookup_result x;
    entity_index named = 0;
    template_id_name(id, x, named);
    uint32_t vt = variable_template_of(named);
    const function_template primary = function_templates[vt - 1];
    if (parameters.empty() || function_templates[vt - 1].name != named ||
        entity_table[primary.entity].enclosing != outer)
        return not_supported(n, "temp.class.spec");
    /* [temp.class.spec]/1: one declared after an instantiation it would
       be used for: not analysed */
    for (const instantiation_record &i : instantiations)
        if (i.template_entity == primary.entity && i.specialization)
            return not_supported(n, "temp.class.spec");
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    r = template_parameter_scope(n, parameters, outer, 0, index, scope, first);
    if (r != analysis_result::well_formed)
        return r;
    owned_array<template_argument> arguments;
    partial_arguments++;
    r = template_id_arguments_in(id, vt, scope, arguments);
    partial_arguments--;
    if (r != analysis_result::well_formed)
        return r;
    bool dependent = false;
    bool identity = arguments.size() == parameters.size();
    for (uint32_t i = 0; i < arguments.size(); i++) {
        const template_argument &a = arguments[i];
        if (depends(type_table, a.type) || a.template_entity)
            dependent = true;
        if (a.kind == template_argument::kind_t::pack)
            for (uint32_t k = 0; k < a.count; k++)
                if (depends(type_table, pack_arguments[a.value + k].type) ||
                    pack_arguments[a.value + k].template_entity)
                    dependent = true;
    }
    for (uint32_t i = 0; i < arguments.size() && identity; i++) {
        entity_index pe = template_parameter_entities[first + i];
        const template_argument &a = arguments[i];
        identity = a.template_entity == pe ||
                   (a.kind == template_argument::kind_t::type &&
                    a.type == entity_table[pe].type) ||
                   (a.kind == template_argument::kind_t::pack && a.count == 1 &&
                    pack_arguments[a.value].count == UINT32_MAX &&
                    pack_arguments[a.value].template_entity == pe);
    }
    /* [temp.class.spec]/9: more specialized than the primary */
    if (!dependent || identity)
        return not_supported(n, "temp.class.spec");
    function_template record;
    record.entity = primary.entity;
    record.declaration = n;
    record.declarator = declaration;
    record.first_parameter = first;
    record.parameter_count = static_cast<uint32_t>(parameters.size());
    record.is_variable = true;
    record.primary = vt;
    record.first_pattern = static_cast<uint32_t>(template_arguments.size());
    record.pattern_count = static_cast<uint32_t>(arguments.size());
    for (const template_argument &a : arguments)
        template_arguments.append(a);
    function_templates.append(record);
    if (!parameters.empty())
        equivalent_partial_specializations(
            static_cast<uint32_t>(function_templates.size()));
    return analysis_result::well_formed;
}

/*
 * [temp.class.spec]/1 (CWG 1980): a partial specialization
 * equivalent to another of the template (each at least as specialized as
 * the other: their arguments the same but for the names of their
 * parameters, void_t<typename T::a> and void_t<typename T::b> both void)
 * redeclares it, which clang reports as a redefinition and then uses
 * neither; the template is not used ([temp.over.link]/7: functionally
 * equivalent but not equivalent, no diagnostic required)
 */
void semantic_analysis::equivalent_partial_specializations(uint32_t added)
{
    const function_template a = function_templates[added - 1];
    for (uint32_t k = 0; k < function_templates.size(); k++) {
        const function_template &o = function_templates[k];
        if (k + 1 == added || o.primary != a.primary || !o.parameter_count)
            continue;
        /* [temp.over.link]/7: the template-heads' constraints equivalent
           too */
        if (!same_constraints(a.declaration, o.declaration))
            continue;
        bool unknown = false;
        bool forward = class_at_least_as_specialized(added, k + 1, unknown);
        bool backward = class_at_least_as_specialized(k + 1, added, unknown);
        if (forward && backward && !unknown)
            function_templates[a.primary - 1].unusable = true;
    }
}

uint32_t semantic_analysis::variable_template_of(entity_index e) const
{
    return template_of_form(e, &function_template::is_variable);
}

/*
 * [temp.pre]/1: "A template defines a family of classes, functions, or
 * variables"; /5: "the init-declarator-list in the declaration shall
 * contain at most one declarator".  One
 * at namespace scope, not declared before, whose init-declarator has an
 * initializer: its decl-specifier-seq and declarator are analysed with the
 * template-parameters' types, the initializer per specialization
 * (instantiate_variable).
 */
analysis_result
semantic_analysis::variable_template_definition(node_index n,
                                                node_index declaration)
{
    entity_index outer = current_scope;
    owned_array<node_index> parameters;
    analysis_result r = template_head_parameters(n, parameters, true);
    if (r != analysis_result::well_formed)
        return r;
    const syntax_child *parts = tree.children(declaration) +
                                attributes_without_effect_before(declaration);
    node_index item = parts[1].index();
    if (tree[item].child_count != 2 ||
        syntax_is(tree, tree.children(item)[1], syntax_kind::requires_clause))
        return not_supported(n, "temp.pre");
    /* (the name is not known before the declarator: the template-parameters
       are checked against it below) */
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    r = template_parameter_scope(n, parameters, outer, 0, index, scope, first);
    if (r != analysis_result::well_formed)
        return r;
    if (default_missing_after_one(parameters))
        return not_supported(n, "temp.param");
    entity_index saved_scope = current_scope;
    uint32_t saved_cv = function_cv;
    current_scope = scope;
    specified s;
    declarator_meaning m;
    r = decl_specifier_seq(parts[0].index(), s, false);
    qualified_type type = s.type;
    if (r == analysis_result::well_formed) {
        function_cv = 0;
        r = declarator(declaration, tree.children(item)[0], type, m);
    }
    current_scope = saved_scope;
    function_cv = saved_cv;
    if (r != analysis_result::well_formed)
        return r;
    if (!m.has_id || m.qualifier || m.qualified_namespace || !s.has_type ||
        s.typedef_ || s.static_ || s.extern_ || s.declares_enumeration ||
        s.gnu_flags || s.aligned || m.operator_function || m.literal_operator ||
        type_table[type.unqualified()].kind == type_kind::function ||
        type_table[type.unqualified()].kind == type_kind::none)
        return not_supported(n, "temp.pre");
    for (uint32_t i = 0; i < tree[parts[0].index()].child_count; i++) {
        syntax_child k = tree.children(parts[0].index())[i];
        if (syntax_is(tree, k, syntax_kind::class_specifier) ||
            syntax_is(tree, k, syntax_kind::elaborated_type_specifier) ||
            syntax_is(tree, k, syntax_kind::enum_specifier))
            return not_supported(n, "temp.pre");
    }
    uint32_t name = tokens.identifier(m.id);
    if (entity_table.find(outer, name))
        return not_supported(n, "temp.pre");
    /* [temp.local]/6: "A template-parameter shall not have the same name
       as the template name." (clang's error) */
    for (uint32_t i = 0; i < parameters.size(); i++)
        if (entity_table[template_parameter_entities[first + i]].name == name)
            return not_supported(n, "temp.local");
    function_template record = new_template(
        name, outer, n, index, first, static_cast<uint32_t>(parameters.size()));
    entity_table[record.entity].defined = true;
    entity_table[record.entity].constexpr_ = s.constexpr_;
    record.definition = declaration;
    record.declarator = declaration;
    record.is_variable = true;
    hold_template_name(record, name, outer);
    return analysis_result::well_formed;
}

/*
 * [temp.inst]/7: "Unless a variable template specialization is a declared
 * specialization, the variable template specialization is implicitly
 * instantiated when it is referenced in a context that requires a
 * variable definition to exist or if the existence of the definition
 * affects the semantics of the program."  Here at any use: the template's
 * simple-declaration analysed again where each template-parameter's name
 * is its argument, the variable made in the template's namespace without
 * binding its name (declare_variable_specialization); inline, as clang
 * makes an implicit instantiation (linkonce_odr).
 */
analysis_result semantic_analysis::variable_template_id(node_index n,
                                                        expression_meaning &m)
{
    lookup_result x;
    entity_index named = 0;
    if (!template_id_name(n, x, named))
        return not_supported(n, "temp.names");
    if (uint32_t ct = concept_of(named))
        if (!x.ambiguous)
            return concept_id(n, ct, m);
    uint32_t vt = variable_template_of(named);
    if (!vt || x.ambiguous)
        return not_supported(n, "temp.names");
    owned_array<template_argument> arguments;
    analysis_result r = template_id_arguments(n, vt, arguments);
    if (r != analysis_result::well_formed)
        return r;
    for (const template_argument &a : arguments) {
        if (depends(type_table, a.type) || a.template_entity)
            return not_supported(n, "temp.dep");
        /* a pack's elements: each known ([temp.variadic]/1) */
        if (a.kind == template_argument::kind_t::pack) {
            if (a.count == UINT32_MAX)
                return not_supported(n, "temp.dep");
            for (uint32_t k = 0; k < a.count; k++)
                if (depends(type_table, pack_arguments[a.value + k].type) ||
                    pack_arguments[a.value + k].template_entity ||
                    pack_arguments[a.value + k].count == UINT32_MAX)
                    return not_supported(n, "temp.dep");
        }
    }
    entity_index e = 0;
    r = instantiate_variable(n, vt, arguments.data(),
                             static_cast<uint32_t>(arguments.size()), e);
    if (r != analysis_result::well_formed)
        return r;
    size_t uses_before = name_uses.size();
    r = named_entity(tree.children(n)[0].index(), e, m);
    /* (clang's lookup of the name finds the template, no claim here) */
    name_uses.resize(uses_before);
    return r;
}

analysis_result
semantic_analysis::instantiate_variable(node_index at, uint32_t vt,
                                        const template_argument *arguments,
                                        uint32_t count, entity_index &e)
{
    e = 0;
    const function_template ft = function_templates[vt - 1];
    if (count != ft.parameter_count)
        return not_supported(at, "temp.inst");
    if (uint32_t prior = find_instantiation(ft.entity, arguments, count)) {
        const instantiation_record &x = instantiations[prior - 1];
        /* (one whose initializer uses itself, or that failed) */
        if (!x.specialization || !x.defined)
            return not_supported(at, "temp.inst");
        e = x.specialization;
        return analysis_result::well_formed;
    }
    if (instantiation_depth >= 64)
        return not_supported(at, "temp.inst");
    /* [temp.class.spec.match] for variable templates: a partial
       specialization's declaration, its parameters the deduced arguments */
    uint32_t pattern = 0;
    owned_array<template_argument> deduced;
    analysis_result matched =
        matching_specialization(at, vt, arguments, count, pattern, deduced);
    if (matched != analysis_result::well_formed)
        return matched;
    uint32_t index = record_instantiation(ft, arguments, count);
    function_template definition = ft;
    if (pattern) {
        definition = function_templates[pattern - 1];
        entity_index bound = entity_table.make(
            entity_kind::block_scope, 0, entity_table[ft.entity].enclosing);
        for (uint32_t i = 0; i < definition.parameter_count; i++)
            bind_template_argument(
                bound,
                template_parameter_entities[definition.first_parameter + i],
                deduced[i]);
        instantiations[index].scope = bound;
        instantiations[index].pattern = pattern;
    }
    entity_index scope = instantiations[index].scope;
    instantiation_context context;
    enter_instantiation(context, scope, definition.declaration);
    template_member_scope = scope;
    expanding = false;
    instantiating = 0;
    variable_instantiating = index + 1;
    instantiations[index].defining = true;
    /* (a partial specialization's declarator-id: its template-id) */
    declarator_template_id = pattern != 0;
    analysis_result r = simple_declaration(definition.declarator);
    declarator_template_id = false;
    instantiations[index].defining = false;
    leave_instantiation(context);
    /* an argument that makes the declaration ill-formed: clang reports it
       at the template, with a note at the use */
    if (r != analysis_result::well_formed ||
        !instantiations[index].specialization)
        return not_supported(at, "temp.inst");
    instantiations[index].defined = true;
    e = instantiations[index].specialization;
    return analysis_result::well_formed;
}

/*
 * [temp.alias]/2: "When a template-id refers to the specialization of an
 * alias template, it is equivalent to the associated type obtained by
 * substitution of its template-arguments for the template-parameters in
 * the defining-type-id of the alias template."  The defining-type-id
 * analysed again where each template-parameter's name is its argument
 * (a dependent argument's type a template's own, which deduction then
 * sees through).
 */
analysis_result semantic_analysis::alias_template_type(node_index id,
                                                       uint32_t at,
                                                       type_index &type)
{
    owned_array<template_argument> arguments;
    analysis_result r = template_id_arguments(id, at, arguments);
    if (r != analysis_result::well_formed)
        return r;
    return alias_template_specialization(id, at, arguments, type);
}

/* the type alias template function_templates[at - 1] denotes with
   arguments (one per parameter, a pack's as one) */
analysis_result semantic_analysis::alias_template_specialization(
    node_index id, uint32_t at, const owned_array<template_argument> &arguments,
    type_index &type)
{
    const function_template ft = function_templates[at - 1];
    analysis_result r = analysis_result::well_formed;
    /* an enclosing template's own pack expanded into the alias's pack
       (Numbers... unbound): its elements are not known.  Forwarded whole
       to a pack parameter (libc++'s __invoke_result_t<_Args...>), the
       parameter's name is bound to the enclosing pack itself, so that the
       alias denotes what its type-id with that pack written there would
       ([temp.alias]/2); anything else is refused */
    owned_array<entity_index> forwarded;
    for (uint32_t i = 0; i < ft.parameter_count; i++) {
        const template_argument &a = arguments[i];
        entity_index whole = 0;
        if (a.kind == template_argument::kind_t::pack)
            for (uint32_t k = 0; k < a.count; k++) {
                const template_argument &x = pack_arguments[a.value + k];
                if (x.count != UINT32_MAX)
                    continue;
                if (a.count != 1 ||
                    !template_parameter_is_pack(ft.first_parameter + i) ||
                    entity_table[x.template_entity].kind !=
                        entity_kind::typedef_name)
                    return not_supported(id, "temp.variadic");
                whole = x.template_entity;
            }
        forwarded.append(whole);
    }
    entity_index scope = entity_table.make(entity_kind::block_scope, 0,
                                           entity_table[ft.entity].enclosing);
    for (uint32_t i = 0; i < ft.parameter_count; i++) {
        entity_index parameter =
            template_parameter_entities[ft.first_parameter + i];
        if (forwarded[i]) {
            entity_table.bind(scope, entity_table[parameter].name,
                              forwarded[i]);
            continue;
        }
        bind_template_argument(scope, parameter, arguments[i]);
    }
    entity_index saved_scope = current_scope;
    token_index saved_horizon = lookup_horizon;
    bool saved_expanding = expanding;
    current_scope = scope;
    lookup_horizon = tree[ft.declaration].first_token;
    expanding = false;
    qualified_type t;
    r = alias_type(ft.definition, t);
    current_scope = saved_scope;
    lookup_horizon = saved_horizon;
    expanding = saved_expanding;
    /* an argument that makes the type ill-formed: clang reports it at the
       alias template, with a note at the use; while a candidate is
       substituted, [temp.alias]/3's deduction failure */
    if (r == analysis_result::ill_formed && substituting)
        return r;
    if (r == analysis_result::ill_formed)
        return not_supported(id, "temp.alias");
    if (r != analysis_result::well_formed)
        return r;
    /* (a cv-qualified type: template_type's callers take no cv) */
    if (t.cv())
        return not_supported(id, "temp.alias");
    /* [temp.alias]/4 (CWG 1558): "when a template-id refers to the
       specialization of an alias template ... any substitution failure in
       the template arguments of the template-id is a substitution failure
       of the instantiation": dependent arguments the type does not keep
       (void_t<typename T::type> is void) are substituted again with each
       instantiation, which a non-dependent type does not record; a
       partial specialization's are (substitute_specialization) */
    if (!partial_arguments &&
        !depends_on_template_parameter(
            qualified_type::make(t.unqualified(), cv_none))) {
        for (const template_argument &a : arguments) {
            bool dependent =
                depends_on_template_parameter(a.type) || a.template_entity;
            if (a.kind == template_argument::kind_t::pack)
                for (uint32_t k = 0; k < a.count; k++)
                    if (depends_on_template_parameter(
                            pack_arguments[a.value + k].type))
                        dependent = true;
            if (dependent)
                return not_supported(id, "temp.alias");
        }
    }
    type = t.unqualified();
    return analysis_result::well_formed;
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
    case type_kind::array:
        /* T[N]: N a non-type template-parameter */
        if (r.entity && (!parameters || (*parameters)[r.entity].value >= from))
            return true;
        return depends(table, r.operand, parameters, from);
    case type_kind::pointer:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
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

/* the scope of the template-parameters ([basic.scope.temp]) in outer, the
   parameters declared in it (declare_template_parameters, reserved the
   template's name), the template's record index-to-be and its first
   parameter */
analysis_result semantic_analysis::template_parameter_scope(
    node_index n, const owned_array<node_index> &parameters, entity_index outer,
    uint32_t reserved, uint32_t &index, entity_index &scope, uint32_t &first)
{
    index = static_cast<uint32_t>(function_templates.size());
    scope = entity_table.make(entity_kind::block_scope, 0, outer);
    first = static_cast<uint32_t>(template_parameter_entities.size());
    return declare_template_parameters(n, parameters, scope, index, reserved);
}

/* whether a template-parameter without a default follows one with a
   default ([temp.param]/14) */
bool semantic_analysis::default_missing_after_one(
    const owned_array<node_index> &parameters) const
{
    for (size_t i = 1; i < parameters.size(); i++)
        if (template_parameter_has_default(parameters[i - 1]) &&
            !template_parameter_has_default(parameters[i]))
            return true;
    return false;
}

/* template_id_arguments with the template-parameters of scope in scope;
   what they record for claims is dropped */
analysis_result semantic_analysis::template_id_arguments_in(
    node_index id, uint32_t ct, entity_index scope,
    owned_array<template_argument> &arguments)
{
    size_t expressions_before = expressions.size();
    size_t uses_before = name_uses.size();
    entity_index saved_scope = current_scope;
    current_scope = scope;
    analysis_result r = template_id_arguments(id, ct, arguments);
    current_scope = saved_scope;
    expressions.resize(expressions_before);
    name_uses.resize(uses_before);
    return r;
}

/* the items of a comma-separated list (written), each marked whether an
   ellipsis follows it (expansion, [temp.variadic]/5); false for an
   ellipsis that follows none or another */
bool semantic_analysis::expansion_list(node_index list,
                                       owned_array<syntax_child> &written,
                                       owned_array<bool> &expansion) const
{
    for (uint32_t k = 0; k < tree[list].child_count; k++) {
        syntax_child a = tree.children(list)[k];
        if (syntax_is_punctuator(tokens, a, preprocessing_op_or_punc::COMMA))
            continue;
        if (syntax_is_punctuator(tokens, a,
                                 preprocessing_op_or_punc::ELLIPSIS)) {
            if (written.empty() || expansion.back())
                return false;
            expansion.back() = true;
            continue;
        }
        written.append(a);
        expansion.append(false);
    }
    return true;
}

/* a declaration's decl-specifier-seq when it is all the declaration has
   (no declarator) and one decl-specifier, a node: that decl-specifier;
   else 0 */
node_index semantic_analysis::sole_decl_specifier(node_index declaration) const
{
    if (tree[declaration].child_count != 2 ||
        !syntax_is(tree, tree.children(declaration)[0],
                   syntax_kind::decl_specifier_seq))
        return 0;
    node_index seq = tree.children(declaration)[0].index();
    if (tree[seq].child_count != 1 || tree.children(seq)[0].is_token())
        return 0;
    return tree.children(seq)[0].index();
}

bool semantic_analysis::depends_on_parameter_from(qualified_type t,
                                                  uint64_t from) const
{
    return depends(type_table, t, &entity_table, from);
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
    /* class-head: class-key identifier base-clause_opt */
    const syntax_node &spec = tree[specifier];
    if (!spec.child_count ||
        !syntax_is(tree, tree.children(specifier)[0], syntax_kind::class_head))
        return not_supported(n, "temp.class");
    node_index head = tree.children(specifier)[0].index();
    syntax_child head_name = class_head_name(head);
    bool specialization =
        head_name.bits &&
        syntax_is(tree, head_name, syntax_kind::simple_template_id);
    analysis_result r = template_head_parameters(
        n, parameters, true, specialization, specialization);
    if (r != analysis_result::well_formed)
        return r;
    /* a partial specialization's requires-clause is checked where it is
       matched (matching_specialization); the primary's are not analysed */
    if (specialization) {
        return class_specialization_definition(n, specifier, parameters);
    }
    if (!head_name.is_token() ||
        tokens[head_name.index()].kind != token_kind::identifier)
        return not_supported(n, "temp.class");
    /* a specialization declares the friend
       functions its class defines in the template's namespace
       ([class.friend]/6), which a lookup there would find; friends have
       no rules yet */
    for (token_index i = spec.first_token; i < spec.end_token; i++)
        if (p.keyword_of(tokens[i]) == standard_keyword::friend_)
            return not_supported(n, "class.friend");
    uint32_t name = tokens.identifier(head_name.index());
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    r = template_parameter_scope(n, parameters, outer, name, index, scope,
                                 first);
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
            if (!template_parameter_has_default(before))
                continue;
            if (template_parameter_has_default(parameters[i]) ||
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
            if (template_parameter_has_default(
                    template_parameter_nodes[first + i - 1]) &&
                !template_parameter_has_default(
                    template_parameter_nodes[first + i]))
                return not_supported(n, "temp.param");
        for (uint32_t i = 0; i < d.parameter_count; i++)
            if (entity_table[template_parameter_entities[first + i]].kind !=
                entity_kind::template_entity)
                entity_table[template_parameter_entities[first + i]]
                    .instantiation = declared;
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
        if (template_parameter_has_default(parameters[i - 1]) &&
            !template_parameter_has_default(parameters[i]) &&
            !template_parameter_is_pack(first + static_cast<uint32_t>(i)))
            return ill_formed(parameters[i],
                              "each template-parameter after one with a "
                              "default template-argument shall have one",
                              "temp.param");
    function_template record = new_template(
        name, outer, n, index, first, static_cast<uint32_t>(parameters.size()));
    entity_index t = record.entity;
    entity_table[t].defined = true;
    record.definition = specifier;
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
    /* [temp.expl.spec]/1 also declares a specialization without defining
       it: class-key simple-template-id ; (an elaborated-type-specifier),
       whose definition may follow */
    bool defines = spec.kind == syntax_kind::class_specifier;
    node_index id = 0;
    if (defines) {
        node_index head = tree.children(specifier)[0].index();
        id = class_head_name(head).index();
    } else {
        id = tree.children(specifier)[1].index();
    }
    lookup_result x;
    entity_index named = 0;
    if (!template_id_name(id, x, named))
        return not_supported(n, rule);
    for (token_index i = spec.first_token; i < spec.end_token; i++)
        if (p.keyword_of(tokens[i]) == standard_keyword::friend_)
            return not_supported(n, "class.friend");
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
       before it is not analysed.  One of an explicit specialization is
       that specialization's own (declared, it is defined at its next
       use), never another's */
    for (const instantiation_record &r : instantiations)
        if (r.template_entity == primary.entity && r.specialization &&
            entity_table[r.specialization].kind == entity_kind::class_type &&
            !(r.pattern && !function_templates[r.pattern - 1].parameter_count))
            return not_supported(n, rule);
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    analysis_result r =
        template_parameter_scope(n, parameters, outer, 0, index, scope, first);
    if (r != analysis_result::well_formed)
        return r;
    /* the arguments, in the parameters' scope; what they record for
       claims is the template's */
    owned_array<template_argument> arguments;
    partial_arguments++;
    r = template_id_arguments_in(id, ct, scope, arguments);
    partial_arguments--;
    if (r != analysis_result::well_formed)
        return r;
    bool dependent = false;
    bool identity = arguments.size() == parameters.size();
    for (uint32_t i = 0; i < arguments.size(); i++) {
        const template_argument &a = arguments[i];
        if (depends(type_table, a.type) || a.template_entity)
            dependent = true;
        if (a.kind == template_argument::kind_t::pack)
            for (uint32_t k = 0; k < a.count; k++)
                if (depends(type_table, pack_arguments[a.value + k].type) ||
                    pack_arguments[a.value + k].template_entity)
                    dependent = true;
    }
    for (uint32_t i = 0; i < arguments.size() && identity; i++) {
        entity_index pe = template_parameter_entities[first + i];
        const template_argument &a = arguments[i];
        identity = a.template_entity == pe ||
                   (a.kind == template_argument::kind_t::type &&
                    a.type == entity_table[pe].type) ||
                   (a.kind == template_argument::kind_t::pack && a.count == 1 &&
                    pack_arguments[a.value].count == UINT32_MAX &&
                    pack_arguments[a.value].template_entity == pe);
    }
    /* [temp.expl.spec]: no dependent argument; [temp.class.spec]/9: "The
       specialization shall be more specialized than the primary template"
       (the primary's own arguments are not; other cases are
       [temp.class.order]'s, not analysed); /9 Example 1's #2: the
       primary's arguments with a requires-clause, more constrained than
       the unconstrained primary ([temp.constr.order]) */
    if (identity && head_constraint(n).bits &&
        !head_constraint(function_templates[ct - 1].declaration).bits)
        identity = false;
    if (parameters.empty() == dependent || identity)
        return not_supported(n, rule);
    function_template record;
    record.entity = primary.entity;
    record.declaration = n;
    record.definition = 0;
    if (defines)
        record.definition = specifier;
    record.first_parameter = first;
    record.parameter_count = static_cast<uint32_t>(parameters.size());
    record.is_class = true;
    record.primary = ct;
    record.first_pattern = static_cast<uint32_t>(template_arguments.size());
    record.pattern_count = static_cast<uint32_t>(arguments.size());
    /* a second explicit specialization of the same arguments: a
       redefinition ([basic.def.odr]/1) */
    for (function_template &o : function_templates) {
        if (o.primary != ct || o.parameter_count || !parameters.empty())
            continue;
        bool same = true;
        for (uint32_t i = 0; i < o.pattern_count && same; i++)
            same = template_arguments[o.first_pattern + i] == arguments[i];
        if (!same)
            continue;
        /* [basic.def]/1: declared again, or defined after its
           declaration (the instantiations that named it define it at
           their next use, instantiate_class) */
        if (!defines)
            return analysis_result::well_formed;
        if (o.definition)
            return not_supported(n, rule);
        o.declaration = n;
        o.definition = specifier;
        return analysis_result::well_formed;
    }
    for (const template_argument &a : arguments)
        template_arguments.append(a);
    function_templates.append(record);
    if (!parameters.empty())
        equivalent_partial_specializations(
            static_cast<uint32_t>(function_templates.size()));
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
 * matching is /1.2's partial ordering ([temp.class.order],
 * temp.func.order.cpp).
 */
/*
 * [temp.class.spec.match]/2-3 (C++20 [temp.spec.partial.match]): "If the
 * template arguments of a partial specialization cannot be deduced
 * because of the structure of its template-parameter-list and the
 * template-id, the program is ill-formed" ... "the deduced template
 * arguments are substituted into" the specialization's arguments: they
 * shall be valid and the same as the use's.  The specialization's
 * template-id analysed again with its parameters bound to what deduction
 * found; a failure there is no match ([temp.deduct]/8), the arguments
 * that aliases fold kept (void_t<typename T::type> of CWG 1558)
 */
analysis_result semantic_analysis::substitute_specialization(
    uint32_t specialization, const owned_array<template_argument> &deduced,
    const template_argument *arguments, uint32_t count, bool &substituted)
{
    substituted = false;
    const function_template s = function_templates[specialization - 1];
    node_index id = 0;
    if (s.is_variable) {
        id = variable_specialization_id(s.declarator);
    } else {
        node_index head = tree.children(s.definition)[0].index();
        id = class_head_name(head).index();
    }
    entity_index scope = entity_table.make(entity_kind::block_scope, 0,
                                           entity_table[s.entity].enclosing);
    for (uint32_t i = 0; i < s.parameter_count; i++)
        bind_template_argument(
            scope, template_parameter_entities[s.first_parameter + i],
            deduced[i]);
    substitution_mark mark = mark_substitution();
    owned_array<template_argument> again;
    token_index saved_horizon = lookup_horizon;
    lookup_horizon = tree[s.declaration].first_token;
    substituting++;
    partial_arguments++;
    analysis_result r = template_id_arguments_in(id, s.primary, scope, again);
    partial_arguments--;
    substituting--;
    lookup_horizon = saved_horizon;
    if (r == analysis_result::ill_formed && undo_substitution(mark))
        return analysis_result::well_formed;
    if (r != analysis_result::well_formed)
        return r;
    if (again.size() != count)
        return analysis_result::well_formed;
    for (uint32_t i = 0; i < count; i++)
        if (!(again[i] == arguments[i]))
            return analysis_result::well_formed;
    substituted = true;
    return analysis_result::well_formed;
}

/* [temp.deduct.type]/8: a partial specialization's argument x against
   the argument y it is matched with, the parameters it names deduced
   into d (set) */
bool semantic_analysis::deduce_argument(const template_argument &x,
                                        const template_argument &y,
                                        owned_array<template_argument> &d,
                                        owned_array<bool> &set)
{
    if (x.kind != y.kind)
        return false;
    if (x.kind == template_argument::kind_t::type)
        return deduce_type(x.type, y.type, d, set, true);
    if (!x.template_entity)
        return x == y;
    if (set[x.value])
        return d[x.value] == y;
    d[x.value] = y;
    set[x.value] = true;
    return true;
}

/* [temp.deduct.type]/9: the elements of a pack argument x (the
   partial specialization's, <I1, In...>) against the elements of y, each
   in turn; an element that is the expansion of the specialization's own
   pack (count UINT32_MAX, its last) takes the rest of y's as its pack */
bool semantic_analysis::deduce_pack_argument(const template_argument &x,
                                             const template_argument &y,
                                             owned_array<template_argument> &d,
                                             owned_array<bool> &set)
{
    for (uint32_t k = 0; k < x.count; k++) {
        const template_argument e = pack_arguments[x.value + k];
        if (e.kind == template_argument::kind_t::pack &&
            e.count == UINT32_MAX) {
            /* /5.7: a pack expansion not at the end is a non-deduced
               context: not analysed */
            if (k + 1 != x.count) {
                deduction_unknowns++;
                return false;
            }
            if (k > y.count)
                return false;
            owned_array<template_argument> rest;
            for (uint32_t r = k; r < y.count; r++)
                rest.append(pack_arguments[y.value + r]);
            template_argument p =
                make_pack(rest.data(), static_cast<uint32_t>(rest.size()));
            uint64_t at = e.value;
            if (set[at])
                return d[at] == p;
            d[at] = p;
            set[at] = true;
            return true;
        }
        if (k >= y.count ||
            !deduce_argument(e, pack_arguments[y.value + k], d, set))
            return false;
    }
    return x.count == y.count;
}

analysis_result semantic_analysis::matching_specialization(
    node_index at, uint32_t class_template, const template_argument *arguments,
    uint32_t count, uint32_t &pattern, owned_array<template_argument> &deduced)
{
    pattern = 0;
    deduced.clear();
    uint32_t matches = 0;
    /* the partial specializations that match, and their deduced
       arguments' place in all */
    owned_array<uint32_t> found, first_deduced;
    owned_array<template_argument> all;
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
        uint32_t unknowns = deduction_unknowns;
        bool match = true;
        for (uint32_t i = 0; i < count && match; i++) {
            const template_argument x = template_arguments[s.first_pattern + i];
            const template_argument &y = arguments[i];
            if (x.kind == template_argument::kind_t::pack &&
                y.kind == template_argument::kind_t::pack)
                match = deduce_pack_argument(x, y, d, set);
            else
                match = deduce_argument(x, y, d, set);
        }
        if (deduction_unknowns != unknowns)
            return not_supported(at, "temp.deduct.type");
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
        bool substituted = false;
        analysis_result again =
            substitute_specialization(k + 1, d, arguments, count, substituted);
        if (again != analysis_result::well_formed)
            return again;
        if (!substituted)
            continue;
        /* [temp.class.spec.match]/2: "and the deduced template arguments
           satisfy the associated constraints of the partial
           specialization, if any" */
        bool satisfied = true;
        analysis_result c = template_constraints_satisfied(k + 1, d, satisfied);
        if (c != analysis_result::well_formed)
            return c;
        if (!satisfied)
            continue;
        matches++;
        found.append(k + 1);
        first_deduced.append(static_cast<uint32_t>(all.size()));
        for (const template_argument &a : d)
            all.append(a);
    }
    if (!matches)
        return analysis_result::well_formed;
    /* [temp.class.spec.match]/1.2: "If more than one matching
       specialization is found, the partial order rules ([temp.class.order])
       are used to determine whether one of the specializations is more
       specialized than the others.  If none of the specializations is more
       specialized than all of the other matching specializations, then the
       use of the class template is ambiguous and the program is
       ill-formed" (the ambiguity, or an order analysis cannot tell:
       unsupported) */
    size_t chosen = found.size();
    for (size_t i = 0; i < found.size() && chosen == found.size(); i++) {
        bool best = true;
        for (size_t j = 0; j < found.size() && best; j++) {
            if (i == j)
                continue;
            bool unknown = false;
            bool forward =
                class_at_least_as_specialized(found[i], found[j], unknown);
            bool backward =
                class_at_least_as_specialized(found[j], found[i], unknown);
            if (unknown)
                return not_supported(at, "temp.class.order");
            best = forward && !backward;
        }
        if (best)
            chosen = i;
    }
    if (chosen == found.size())
        return not_supported(at, "temp.class.order");
    pattern = found[chosen];
    deduced.clear();
    for (uint32_t i = 0; i < function_templates[pattern - 1].parameter_count;
         i++)
        deduced.append(all[first_deduced[chosen] + i]);
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
    if (!named && !qualified && is_builtin_common_type(name))
        return builtin_common_type(id, type);
    uint32_t ct = class_template_of(named);
    if (!ct) {
        uint32_t at = alias_template_of(named);
        if (at)
            return alias_template_type(id, at, type);
        return not_supported(id, "temp.names");
    }
    owned_array<template_argument> arguments;
    analysis_result filled = template_id_arguments(id, ct, arguments);
    if (filled != analysis_result::well_formed)
        return filled;
    /* [temp.dep.type]/9: "the template name is a template parameter" */
    bool dependent = function_templates[ct - 1].is_parameter;
    for (const template_argument &a : arguments) {
        if (depends(type_table, a.type) || a.template_entity)
            dependent = true;
        /* a pack of dependent arguments (deduced from by
           deduce_pack_argument, [temp.deduct.type]/9) */
        if (a.kind == template_argument::kind_t::pack)
            for (uint32_t k = 0; k < a.count; k++)
                if (depends(type_table, pack_arguments[a.value + k].type) ||
                    pack_arguments[a.value + k].template_entity)
                    dependent = true;
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
            if (!expansion_list(parts[i].index(), written, expansion))
                return not_supported(id, "temp.variadic");
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
                /* a partial specialization's own pack, In... (its
                   pattern the pack's name alone): the rest of the
                   primary's pack, deduced when it is matched
                   (matching_specialization) */
                uint32_t own = 0;
                if (end == first + 1 &&
                    tokens[first].kind == token_kind::identifier)
                    own = own_pack_parameter(tokens.identifier(first));
                if (own) {
                    if (arguments.size() < pack || pack >= count)
                        return not_supported(id, "temp.variadic");
                    template_argument v;
                    v.kind = template_argument::kind_t::pack;
                    v.template_entity = template_parameter_entities[own - 1];
                    v.value = entity_table[v.template_entity].value;
                    v.count = UINT32_MAX;
                    elements.append(v);
                    continue;
                }
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

/* the template parameter pack name names from the current scope (a
   partial specialization's own, not bound to elements), its index in
   template_parameter_entities plus one; else 0 */
uint32_t semantic_analysis::own_pack_parameter(uint32_t name) const
{
    for (entity_index s = current_scope;; s = entity_table[s].enclosing) {
        entity_index e = entity_table.find(s, name);
        if (e) {
            for (size_t i = template_parameter_entities.size(); i-- > 0;)
                if (template_parameter_entities[i] == e &&
                    template_parameter_is_pack(static_cast<uint32_t>(i)))
                    return static_cast<uint32_t>(i + 1);
            return 0;
        }
        if (!s)
            return 0;
    }
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
        if (a.kind == entity_kind::template_entity) {
            /* template template-parameters: their own parameters */
            const function_template x = function_templates[a.instantiation - 1];
            const function_template y = function_templates[b.instantiation - 1];
            if (x.parameter_count != y.parameter_count ||
                !same_template_parameters(x.first_parameter, y.first_parameter,
                                          x.parameter_count))
                return false;
        }
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
    analysis_result r = template_head_parameters(n, parameters, true);
    if (r != analysis_result::well_formed)
        return r;
    uint32_t name = tokens.identifier(parts[1].index());
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    r = template_parameter_scope(n, parameters, outer, name, index, scope,
                                 first);
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
            if (template_parameter_has_default(x))
                return not_supported(n, "temp.param");
        entity_table.add_declaration(d.entity, n);
        return analysis_result::well_formed;
    }
    /* [temp.param]/14 (a later declaration may not supply the defaults
       before) */
    if (default_missing_after_one(parameters))
        return not_supported(n, "temp.param");
    function_template record = new_template(
        name, outer, n, index, first, static_cast<uint32_t>(parameters.size()));
    record.is_class = true;
    hold_template_name(record, name, outer);
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
        if (template_parameter_has_default(x))
            return not_supported(x, "temp.param");
    node_index id = out_of_class_member(definition);
    lookup_result x;
    entity_index named = 0;
    if (!template_id_name(id, x, named))
        return not_supported(n, "temp.mem.func");
    uint32_t ct = class_template_of(named);
    if (!ct || function_templates[ct - 1].name != named ||
        function_templates[ct - 1].primary ||
        !function_templates[ct - 1].definition ||
        entity_table[function_templates[ct - 1].entity].enclosing != outer)
        return not_supported(n, "temp.mem.func");
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    r = template_parameter_scope(n, parameters, outer, 0, index, scope, first);
    if (r != analysis_result::well_formed)
        return r;
    /* the template-id's arguments in terms of the parameters; what they
       record for claims is the template's */
    owned_array<template_argument> arguments;
    r = template_id_arguments_in(id, ct, scope, arguments);
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
    instantiation_context context;
    enter_instantiation(context, scope, o.declaration);
    template_member_scope = scope;
    declaration_access = 0;
    instantiating = 0;
    class_instantiating = 0;
    analysis_result r = function_definition(o.definition);
    leave_instantiation(context);
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
        d.has_unsupported = unsupported.size() > unsupported_before;
    }
    return analysis_result::unsupported;
}

/* a template's entity named name in outer, declared by n
   (function_templates[index]), and its record of parameters
   template_parameter_entities[first .. + count); [basic.link]/4: a name of
   an unnamed namespace has internal linkage */
semantic_analysis::function_template
semantic_analysis::new_template(uint32_t name, entity_index outer, node_index n,
                                uint32_t index, uint32_t first, uint32_t count)
{
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
    record.parameter_count = count;
    return record;
}

/* the template's name in outer, held by an entity of its own
   (held_by_template), and its record kept */
void semantic_analysis::hold_template_name(function_template &record,
                                           uint32_t name, entity_index outer)
{
    entity_index held = entity_table.make(entity_kind::none, name, outer);
    entity_table[held].held_by_template = true;
    entity_table.bind(outer, name, held);
    record.name = held;
    function_templates.append(record);
}

/* the number of a simple-declaration's leading attribute-specifier-seqs
   and gnu-attribute-specifiers whose attributes change nothing vcc
   generates */
uint32_t semantic_analysis::attributes_without_effect_before(
    node_index declaration) const
{
    uint32_t k = 0;
    while (k < tree[declaration].child_count &&
           class_head_attributes_without_effect(tree.children(declaration)[k]))
        k++;
    return k;
}

/* [temp.variadic]/7: whether each of the tokens [first, end) that is the
   pack's name is directly followed by ... (a pattern of the pack alone,
   A...) */
bool semantic_analysis::pack_only_expanded(uint32_t name, token_index first,
                                           token_index end) const
{
    for (token_index t = first; t < end; t++)
        if (tokens[t].kind == token_kind::identifier &&
            tokens.identifier(t) == name &&
            (t + 1 >= end ||
             !syntax_is_punctuator(tokens, syntax_child::token(t + 1),
                                   preprocessing_op_or_punc::ELLIPSIS)))
            return false;
    return true;
}

} // namespace cplusplus
