/*
 * temp.arg.cpp: template arguments ([temp.arg]): which types a non-type
 * template-parameter may have ([temp.param]/6), each argument checked
 * against its parameter (type, non-type, template), the default
 * template-arguments, and the parameters' names bound to the arguments.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [temp.param]/6: "A non-type template-parameter shall have one of the
 * following (possibly cv-qualified) types: a structural type (see below),
 * a type that contains a placeholder type, or a placeholder for a deduced
 * class type."  Of /7's structural types, those with rules here: integral
 * and enumeration types, std::nullptr_t, pointers to objects and
 * functions, lvalue references to them (no placeholder: auto is
 * [dcl.spec.auto]'s).
 */
bool semantic_analysis::structural_parameter_type(qualified_type t) const
{
    const struct type &r = type_table[t.unqualified()];
    if (is_integral(t) || r.kind == type_kind::enumeration)
        return true;
    if (r.kind == type_kind::fundamental)
        return r.fundamental == fundamental_type::std_nullptr_t;
    if (r.kind != type_kind::pointer && r.kind != type_kind::lvalue_reference)
        return false;
    const struct type &to = type_table[r.operand.unqualified()];
    return to.kind == type_kind::function ||
           to.kind == type_kind::fundamental || to.kind == type_kind::pointer ||
           to.kind == type_kind::enumeration ||
           to.kind == type_kind::class_type;
}

/* a non-type argument that is an object's or function's address (a
   pointer or reference parameter's) rather than a value */
bool semantic_analysis::address_parameter_type(qualified_type t) const
{
    type_kind k = type_table[t.unqualified()].kind;
    return k == type_kind::pointer || k == type_kind::lvalue_reference;
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
    if (!name)
        return;
    if (a.template_entity) {
        /* a value-dependent argument: the parameter it names */
        entity_table.bind(scope, name, a.template_entity);
        return;
    }
    if (a.kind == template_argument::kind_t::template_name) {
        /* [temp.arg.template]: the class template's name */
        for (const function_template &t : function_templates)
            if (t.entity == static_cast<entity_index>(a.value) && t.is_class &&
                !t.primary) {
                entity_table.bind(scope, name, t.name);
                return;
            }
        return;
    }
    entity_index e = 0;
    const struct type &at = type_table[a.type.unqualified()];
    if (a.kind == template_argument::kind_t::value &&
        (address_parameter_type(a.type) ||
         (at.kind == type_kind::fundamental &&
          at.fundamental == fundamental_type::std_nullptr_t))) {
        /* an address or null pointer: an entity whose uses have no rules
           yet but as another template-argument (template_argument_for) */
        e = entity_table.make(entity_kind::none, name, scope);
        entity_table[e].value = a.value;
    } else if (a.kind == template_argument::kind_t::value) {
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
    if (entity_table[p].kind == entity_kind::template_entity)
        return template_template_argument(at, p, written, out);
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
        /* in an instantiation, a pointer, reference or std::nullptr_t
           parameter's name (bind_template_argument): its argument */
        if (named && entity_table[named].kind == entity_kind::none &&
            !entity_table[named].instantiation && entity_table[named].defined &&
            entity_table[named].type == to) {
            out.kind = template_argument::kind_t::value;
            out.type = to;
            out.value = entity_table[named].value;
            return analysis_result::well_formed;
        }
    }
    if (address_parameter_type(to))
        return address_argument(at, to, written, out);
    const struct type &tr = type_table[to.unqualified()];
    if (tr.kind == type_kind::fundamental &&
        tr.fundamental == fundamental_type::std_nullptr_t) {
        /* (nullptr itself; another constant of the type is not followed) */
        if (!written.is_token() ||
            this->p.keyword_of(tokens[written.index()]) !=
                standard_keyword::nullptr_)
            return not_supported(at, "temp.arg.nontype");
        out.kind = template_argument::kind_t::value;
        out.type = to;
        return analysis_result::well_formed;
    }
    expression_meaning m;
    analysis_result r = expression(written, m);
    if (r == analysis_result::well_formed)
        r = operand_value(at, m);
    if (r != analysis_result::well_formed)
        return r;
    if (tr.kind == type_kind::enumeration) {
        /* [expr.const]/10's converted constant expression of an
           enumeration type: one of that type (no conversion) */
        if (m.type.unqualified() != to.unqualified())
            return not_supported(at, "temp.arg.nontype");
        bool constant = false;
        r = constant_expression(at, m, constant);
        if (r != analysis_result::well_formed)
            return r;
        if (!constant)
            return not_supported(at, "temp.arg.nontype");
        out.kind = template_argument::kind_t::value;
        out.type = to;
        out.value = m.value;
        return analysis_result::well_formed;
    }
    if (!is_integral(m.type) || !is_integral(to))
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
 * [temp.arg.nontype]/2: a converted constant expression of a pointer or
 * reference type, whose value /3 restricts: "For a non-type
 * template-parameter of reference or pointer type ... the reference or
 * pointer value shall not refer to or be the address of (respectively): a
 * temporary object, a string literal object, the result of a typeid
 * expression, a predefined __func__ variable, or a subobject of one of the
 * above."  Here &id or id naming a variable of namespace scope (a function,
 * an array decaying to the pointer) of exactly the type referred to, or
 * nullptr: the argument's value is the entity (0 for a null pointer).
 */
analysis_result semantic_analysis::address_argument(node_index at,
                                                    qualified_type to,
                                                    syntax_child written,
                                                    template_argument &out)
{
    const struct type r = type_table[to.unqualified()];
    bool reference = r.kind == type_kind::lvalue_reference;
    out.kind = template_argument::kind_t::value;
    out.type = to;
    if (!reference && written.is_token() &&
        p.keyword_of(tokens[written.index()]) == standard_keyword::nullptr_)
        return analysis_result::well_formed;
    syntax_child operand = written;
    bool address_of = false;
    if (syntax_is(tree, written, syntax_kind::unary_expression) &&
        tree[written.index()].child_count == 2 &&
        syntax_is_punctuator(tokens, tree.children(written.index())[0],
                             preprocessing_op_or_punc::AMPERSAND)) {
        operand = tree.children(written.index())[1];
        address_of = true;
    }
    if (reference && address_of)
        return not_supported(at, "temp.arg.nontype");
    expression_meaning m;
    analysis_result result = expression(operand, m);
    if (result != analysis_result::well_formed)
        return result;
    entity_index e = m.entity;
    if (!e)
        return not_supported(at, "temp.arg.nontype");
    const entity x = entity_table[e];
    if ((x.kind != entity_kind::variable && x.kind != entity_kind::function) ||
        x.instantiation || x.next_overload || !is_namespace(x.enclosing))
        return not_supported(at, "temp.arg.nontype");
    const struct type &xr = type_table[x.type.unqualified()];
    bool same = false;
    if (reference || address_of)
        same = r.operand == x.type && xr.kind != type_kind::lvalue_reference &&
               xr.kind != type_kind::rvalue_reference;
    else if (xr.kind == type_kind::array)
        same = r.operand == xr.operand;
    else if (xr.kind == type_kind::function)
        same = r.operand == x.type;
    if (!same)
        return not_supported(at, "temp.arg.nontype");
    out.value = e;
    return analysis_result::well_formed;
}

/*
 * [temp.arg.template]/1: "A template-argument for a template
 * template-parameter shall be the name of a class template or an alias
 * template, expressed as id-expression."  A class template here (as a
 * type-id the parser made of the name, or an id-expression), qualified or
 * not, or in a template's own declaration a template template-parameter;
 * /3's matching only when the parameters are the same (anything else is
 * unsupported, not judged).
 */
analysis_result semantic_analysis::template_template_argument(
    node_index at, entity_index parameter, syntax_child written,
    template_argument &out)
{
    /* the written name: identifier, or nested-name-specifier identifier */
    syntax_child name = written;
    node_index qualifier = 0;
    if (syntax_is(tree, name, syntax_kind::type_id)) {
        if (tree[name.index()].child_count != 1 ||
            !syntax_is(tree, tree.children(name.index())[0],
                       syntax_kind::type_specifier_seq))
            return not_supported(at, "temp.arg.template");
        name = tree.children(name.index())[0];
        if (tree[name.index()].child_count != 1)
            return not_supported(at, "temp.arg.template");
        name = tree.children(name.index())[0];
        if (syntax_is(tree, name, syntax_kind::simple_type_specifier)) {
            const syntax_child *c = tree.children(name.index());
            if (tree[name.index()].child_count != 2 ||
                !syntax_is(tree, c[0], syntax_kind::nested_name_specifier))
                return not_supported(at, "temp.arg.template");
            qualifier = c[0].index();
            name = c[1];
        }
    } else if (syntax_is(tree, name, syntax_kind::qualified_id)) {
        const syntax_child *c = tree.children(name.index());
        if (tree[name.index()].child_count != 2 ||
            !syntax_is(tree, c[0], syntax_kind::nested_name_specifier))
            return not_supported(at, "temp.arg.template");
        qualifier = c[0].index();
        name = c[1];
    }
    if (!name.is_token() || tokens[name.index()].kind != token_kind::identifier)
        return not_supported(at, "temp.arg.template");
    lookup_result x;
    if (qualifier) {
        nominated_scope scope;
        analysis_result r = nested_name_specifier(qualifier, scope);
        if (r != analysis_result::well_formed)
            return r;
        if (scope.entity && !is_namespace(scope.entity))
            return not_supported(at, "temp.arg.template");
        x = lookup_qualified(scope.entity, tokens.identifier(name.index()),
                             lookup_filter::any);
    } else {
        x = lookup_unqualified(tokens.identifier(name.index()),
                               lookup_filter::any);
    }
    entity_index named = x.entity;
    if (!named)
        named = x.unknown_entity;
    /* (an injected-class-name names its class template here,
       [temp.local]/1) */
    uint32_t a = class_template_of(named);
    if (!a || function_templates[a - 1].primary)
        return not_supported(at, "temp.arg.template");
    const function_template given = function_templates[a - 1];
    const function_template wanted =
        function_templates[entity_table[parameter].instantiation - 1];
    if (given.parameter_count != wanted.parameter_count ||
        !same_template_parameters(given.first_parameter, wanted.first_parameter,
                                  wanted.parameter_count))
        return not_supported(at, "temp.arg.template");
    out.kind = template_argument::kind_t::template_name;
    if (given.is_parameter) {
        out.template_entity = given.entity;
        out.value = entity_table[given.entity].value;
        return analysis_result::well_formed;
    }
    out.value = given.entity;
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
        template_parameter_parts form;
        if (!template_parameter_form(node, form) || !form.has_default)
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
            at, template_index, i, form.default_argument, arguments.data(), a);
        current_scope = saved;
        lookup_horizon = saved_horizon;
        if (r != analysis_result::well_formed)
            return r;
        arguments[i] = a;
    }
    return analysis_result::well_formed;
}

} // namespace cplusplus
