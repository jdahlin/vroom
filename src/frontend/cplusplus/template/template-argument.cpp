/*
 * template-argument.cpp: template arguments ([temp.arg]): which types a
 * non-type template-parameter may have ([temp.param]/6), each argument checked
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

/* [temp.dep.type]/9: a pack bound to the arguments of a specialization
   (make_bound_pack) one of which names a dependent type, as when an alias
   template is used with an enclosing template's parameters */
bool semantic_analysis::bound_pack_is_dependent(entity_index e) const
{
    const entity &p = entity_table[e];
    if (p.kind != entity_kind::pack)
        return false;
    for (uint32_t i = 0; i < p.member_count; i++) {
        const entity &x = entity_table[pack_elements[p.value + i]];
        if (x.kind == entity_kind::typedef_name &&
            depends_on_template_parameter(x.type))
            return true;
    }
    return false;
}

/* the parameter's name in scope as its argument: [temp.arg.type]'s type,
   or [temp.arg.nontype]'s value, a prvalue ([temp.param]/8) that
   enumerator_value gives as an enumerator's */
void semantic_analysis::bind_template_argument(entity_index scope,
                                               entity_index parameter,
                                               const template_argument &a)
{
    uint32_t name = entity_table[parameter].name;
    if (a.kind == template_argument::kind_t::pack && !name)
        return;
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
        /* [temp.arg.template]: the class or alias template's name */
        for (const function_template &t : function_templates)
            if (t.entity == static_cast<entity_index>(a.value) &&
                (t.is_class || t.is_alias) && !t.primary) {
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

/* what [abi:mangle.template-arg] spells of a value-dependent argument
   (entity_kind::dependent_value): [!] Q<args>::name, Q unqualified as
   written (clang mangles the qualifier as written) */
entity_index semantic_analysis::dependent_value_entity(SyntaxElement written)
{
    entity_index e = entity_table.make(entity_kind::dependent_value, 0, 0);
    bool negated = false;
    if (written.is(syntax_kind::unary_expression) && written.size() == 2 &&
        written[0].is(preprocessing_op_or_punc::EXCLAMATION)) {
        negated = true;
        written = written[1];
    }
    /* an alias template's parameter bound to such an argument: that
       argument */
    if (!negated && written.is_token() && written.is_identifier()) {
        lookup_result x = lookup_unqualified(tokens.identifier(written.token()),
                                             lookup_filter::any);
        if (x.entity &&
            entity_table[x.entity].kind == entity_kind::dependent_value)
            return x.entity;
    }
    if (!negated && written.is(syntax_kind::simple_template_id))
        return dependent_variable_specialization(written, e);
    if (!written.is(syntax_kind::qualified_id) || written.size() != 2)
        return e;
    SyntaxElement qualifier = written[0];
    SyntaxElement member = written[1];
    if (!member.is_token() ||
        tokens[member.token()].kind != token_kind::identifier ||
        !qualifier.is(syntax_kind::nested_name_specifier) ||
        qualifier.size() != 2 ||
        !qualifier[0].is(syntax_kind::simple_template_id))
        return e;
    /* (its own unsupported records: the argument stays value-dependent) */
    size_t unsupported_before = unsupported.size();
    type_index q = 0;
    analysis_result r = template_type(qualifier[0], 0, false, q);
    unsupported.resize(unsupported_before);
    if (r != analysis_result::well_formed ||
        type_table[q].kind != type_kind::dependent ||
        entity_table[type_table[q].entity].kind !=
            entity_kind::template_specialization)
        return e;
    qualified_type type = qualified_type::make(q, cv_none);
    uint32_t name = tokens.identifier(member.token());
    uint64_t value = 1;
    if (negated)
        value = 3;
    for (entity_index d : dependent_values)
        if (entity_table[d].type == type && entity_table[d].name == name &&
            entity_table[d].value == value)
            return d;
    entity_table[e].type = type;
    entity_table[e].name = name;
    entity_table[e].value = value;
    dependent_values.append(e);
    return e;
}

/* [temp.dep.constexpr]/2: a variable template's specialization v<args>
   with a dependent argument, as a value-dependent template-argument
   (enable_if_t<is_fp<T>, bool>): a dependent specialization of the
   variable template (one per arguments), which [abi:mangle.template-arg]
   spells X <source-name> <template-args> E (value 4); e if it is not
   one */
entity_index
semantic_analysis::dependent_variable_specialization(SyntaxRef id,
                                                     entity_index e)
{
    if (id.size() < 3 || !id[0].is_identifier())
        return e;
    lookup_result x = lookup_unqualified(tokens.identifier(id[0].token()),
                                         lookup_filter::any);
    entity_index named = x.entity;
    if (!named)
        named = x.unknown_entity;
    uint32_t vt = variable_template_of(named);
    if (!vt)
        return e;
    size_t unsupported_before = unsupported.size();
    owned_array<template_argument> arguments;
    analysis_result r = template_id_arguments(id, vt, arguments);
    unsupported.resize(unsupported_before);
    if (r != analysis_result::well_formed)
        return e;
    bool dependent = false;
    for (const template_argument &a : arguments)
        if (a.kind == template_argument::kind_t::pack ||
            depends_on_template_parameter(a.type) || a.template_entity)
            dependent = true;
    if (!dependent)
        return e;
    entity_index v = function_templates[vt - 1].entity;
    entity_index s = 0;
    for (const instantiation_record &i : instantiations) {
        if (i.template_entity != v || i.argument_count != arguments.size() ||
            !i.specialization ||
            entity_table[i.specialization].kind !=
                entity_kind::template_specialization)
            continue;
        bool same = true;
        for (uint32_t k = 0; k < i.argument_count && same; k++)
            same = template_arguments[i.first_argument + k] == arguments[k];
        if (same)
            s = i.specialization;
    }
    if (!s) {
        instantiation_record record;
        record.template_entity = v;
        record.first_argument =
            static_cast<uint32_t>(template_arguments.size());
        record.argument_count = static_cast<uint32_t>(arguments.size());
        for (const template_argument &a : arguments)
            template_arguments.append(a);
        s = entity_table.make(entity_kind::template_specialization,
                              entity_table[v].name, entity_table[v].enclosing);
        entity_table[s].type = qualified_type::make(
            type_table.of_entity(type_kind::dependent, s), cv_none);
        entity_table[s].instantiation =
            static_cast<uint32_t>(instantiations.size()) + 1;
        record.specialization = s;
        instantiations.append(record);
    }
    qualified_type type = entity_table[s].type;
    for (entity_index d : dependent_values)
        if (entity_table[d].type == type && entity_table[d].value == 4)
            return d;
    entity_table[e].type = type;
    entity_table[e].value = 4;
    dependent_values.append(e);
    return e;
}

analysis_result semantic_analysis::substituted_parameter_type(
    uint32_t template_index, uint32_t position, const template_argument *before,
    qualified_type &type)
{
    const function_template ft = function_templates[template_index - 1];
    SyntaxRef node = template_parameter_nodes[ft.first_parameter + position];
    template_parameter_parts form;
    if (!template_parameter_form(node, form))
        return not_supported(node, "temp.param");
    entity_index scope = entity_table.make(entity_kind::block_scope, 0,
                                           entity_table[ft.entity].enclosing);
    for (uint32_t k = 0; k < position; k++)
        bind_template_argument(
            scope, template_parameter_entities[ft.first_parameter + k],
            before[k]);
    entity_index saved = current_scope;
    token_index saved_horizon = lookup_horizon;
    current_scope = scope;
    lookup_horizon = ft.declaration.first_token();
    specified sp;
    analysis_result r = decl_specifier_seq(node[0], sp, false);
    qualified_type t = sp.type;
    if (r == analysis_result::well_formed && form.has_declarator) {
        declarator_meaning dm;
        flag_set pack(pack_declarator, true);
        r = declarator(node, form.declarator, t, dm);
    }
    current_scope = saved;
    lookup_horizon = saved_horizon;
    if (r != analysis_result::well_formed)
        return r;
    type = qualified_type::make(t.unqualified(), cv_none);
    return analysis_result::well_formed;
}

/*
 * [temp.arg.type]/1: "A template-argument for a template-parameter which
 * is a type shall be a type-id."  [temp.arg.nontype]/2: "A template-argument
 * for a non-type template-parameter shall be a converted constant
 * expression of the type of the template-parameter" (integral here).
 */
analysis_result semantic_analysis::template_argument_for(
    SyntaxRef at, uint32_t template_index, uint32_t position,
    SyntaxElement written, const template_argument *before,
    template_argument &out)
{
    const function_template ft = function_templates[template_index - 1];
    entity_index p = template_parameter_entities[ft.first_parameter + position];
    out = template_argument();
    if (entity_table[p].kind == entity_kind::template_entity)
        return template_template_argument(at, p, written, out);
    if (entity_table[p].kind == entity_kind::typedef_name) {
        if (!written.is(syntax_kind::type_id))
            return not_supported(at, "temp.arg");
        return type_id(written, out.type);
    }
    if (written.is(syntax_kind::type_id))
        return not_supported(at, "temp.arg");
    qualified_type to = entity_table[p].type;
    /* [temp.param]/4: a parameter whose type is an earlier type parameter
       of the template (template <class T, T v>): that argument's type, the
       cv-qualifiers ignored (/6) */
    uint32_t typed_by = own_type_parameter_position(
        to, entity_table[p].enclosing, ft.first_parameter);
    if (typed_by != UINT32_MAX) {
        if (!before || typed_by >= position ||
            before[typed_by].kind != template_argument::kind_t::type)
            return not_supported(at, "temp.arg.nontype");
        to = qualified_type::make(before[typed_by].type.unqualified(), cv_none);
        /* (a type that is itself a template parameter, in a partial
           specialization's arguments: a name of the same type or a
           value-dependent expression below, else refused there) */
        if (!depends_on_template_parameter(to) &&
            !structural_parameter_type(to))
            return not_supported(at, "temp.arg.nontype");
    } else if (depends_on_template_parameter(to)) {
        /* another dependent type: the parameter's type with the arguments
           before it, which may fail to be one ([temp.deduct]/8) */
        if (!before)
            return not_supported(at, "temp.arg.nontype");
        VTRY(r,
             substituted_parameter_type(template_index, position, before, to));
        /* still dependent (template <class C, typename C::type = 0> with C
           a template parameter): the argument is value-dependent
           ([temp.dep.constexpr]/2), checked against the parameter's type
           ([temp.arg.nontype]/2) once an instantiation gives C */
        if (depends_on_template_parameter(to)) {
            out.kind = template_argument::kind_t::value;
            out.type = to;
            out.template_entity = dependent_value_entity(written);
            out.value = UINT32_MAX;
            return analysis_result::well_formed;
        }
        if (!structural_parameter_type(to))
            return not_supported(at, "temp.arg.nontype");
    }
    /* in a template's own declaration, a non-type template-parameter's
       name: a value dependent argument ([temp.dep.constexpr]/2), of a
       parameter of the same type */
    token_index first = written.token();
    token_index end = first + 1;
    if (!written.is_token()) {
        first = written.first_token();
        end = written.end_token();
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
        /* an alias template's parameter bound to a value-dependent
           argument (enable_if_t<is_integral<T>::value, int>): that
           argument */
        if (named && entity_table[named].kind == entity_kind::dependent_value) {
            out.kind = template_argument::kind_t::value;
            out.type = to;
            out.template_entity = named;
            out.value = UINT32_MAX;
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
    /* [temp.dep.constexpr]/2: another expression naming a template
       parameter (is_integral<T>::value): value-dependent, not analysed
       until an instantiation gives the parameters values */
    for (token_index t = first; t < end; t++) {
        if (tokens[t].kind != token_kind::identifier)
            continue;
        lookup_result x =
            lookup_unqualified(tokens.identifier(t), lookup_filter::any);
        entity_index e = x.entity;
        if (!e)
            e = find_pack(tokens.identifier(t));
        if (!e)
            continue;
        const entity &named = entity_table[e];
        /* (or an alias template's parameter bound to such an argument) */
        bool parameter = (named.kind == entity_kind::none &&
                          named.instantiation && !named.held_by_template) ||
                         named.kind == entity_kind::dependent_value ||
                         bound_pack_is_dependent(e);
        if (!parameter && (named.kind == entity_kind::none ||
                           !depends_on_template_parameter(named.type)))
            continue;
        if (!is_integral(to) &&
            type_table[to.unqualified()].kind != type_kind::enumeration)
            return not_supported(at, "temp.dep.constexpr");
        out.kind = template_argument::kind_t::value;
        out.type = to;
        out.template_entity = dependent_value_entity(written);
        out.value = UINT32_MAX;
        return analysis_result::well_formed;
    }
    if (address_parameter_type(to))
        return address_argument(at, to, written, out);
    const struct type &tr = type_table[to.unqualified()];
    if (tr.kind == type_kind::fundamental &&
        tr.fundamental == fundamental_type::std_nullptr_t) {
        /* (nullptr itself; another constant of the type is not followed) */
        if (!written.is_token() ||
            this->p.keyword_of(tokens[written.token()]) !=
                standard_keyword::nullptr_)
            return not_supported(at, "temp.arg.nontype");
        out.kind = template_argument::kind_t::value;
        out.type = to;
        return analysis_result::well_formed;
    }
    /* the kind read before the expression's analysis, which may add types
       (and move the table tr is in) */
    type_kind to_kind = tr.kind;
    expression_meaning m;
    analysis_result r = expression(written, m);
    if (r == analysis_result::well_formed)
        VTRY_SET(r, operand_value(at, m));
    if (to_kind == type_kind::enumeration) {
        /* [expr.const]/10's converted constant expression of an
           enumeration type: one of that type (no conversion) */
        if (m.type.unqualified() != to.unqualified())
            return not_supported(at, "temp.arg.nontype");
        bool constant = false;
        VTRY_SET(r, constant_expression(at, m, constant));
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
        VTRY_SET(r, converted_constant_expression(at, m, fundamental_of(to),
                                                  constant));
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
analysis_result semantic_analysis::address_argument(SyntaxRef at,
                                                    qualified_type to,
                                                    SyntaxElement written,
                                                    template_argument &out)
{
    const struct type r = type_table[to.unqualified()];
    bool reference = r.kind == type_kind::lvalue_reference;
    out.kind = template_argument::kind_t::value;
    out.type = to;
    if (!reference && written.is_token() &&
        written.keyword() == standard_keyword::nullptr_)
        return analysis_result::well_formed;
    SyntaxElement operand = written;
    bool address_of = false;
    if (written.is(syntax_kind::unary_expression) && written.size() == 2 &&
        written[0].is(preprocessing_op_or_punc::AMPERSAND)) {
        operand = written[1];
        address_of = true;
    }
    if (reference && address_of)
        return not_supported(at, "temp.arg.nontype");
    expression_meaning m;
    VTRY(result, expression(operand, m));
    entity_index e = m.entity;
    if (!e)
        return not_supported(at, "temp.arg.nontype");
    const entity x = entity_table[e];
    if ((x.kind != entity_kind::variable && x.kind != entity_kind::function) ||
        x.instantiation || x.next_overload || !is_namespace(x.enclosing) ||
        x.c11_thread_local)
        /* (an object of thread storage duration has no address that is
           the same in each thread: [temp.arg.nontype]/2's constant
           expression is clang's error) */
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
 * [temp.arg.template]/3: the argument's template-parameter-list matches
 * the parameter's when each parameter of the one has the same form
 * (kind, and a non-type parameter's type) as the other's; a pack of the
 * parameter's list matches zero or more of the argument's parameters of
 * its form; a pack of the argument's accepts any number of the
 * parameter's; and one the parameter's list leaves over has a default
 * template-argument or is a pack (P0522R0, as clang's default).  A
 * template template-parameter among them matches only when its own list
 * is the same (anything else is unsupported, by the caller).
 */
bool semantic_analysis::template_parameters_match(
    const function_template &wanted, const function_template &given)
{
    if (given.parameter_count == wanted.parameter_count &&
        same_template_parameters(given.first_parameter, wanted.first_parameter,
                                 wanted.parameter_count))
        return true;
    auto same_form = [&](uint32_t i, uint32_t j) {
        const entity &x =
            entity_table[template_parameter_entities[wanted.first_parameter +
                                                     i]];
        const entity &y =
            entity_table[template_parameter_entities[given.first_parameter +
                                                     j]];
        if (x.kind != y.kind)
            return false;
        if (x.kind == entity_kind::none)
            return x.type == y.type;
        if (x.kind == entity_kind::template_entity) {
            const function_template p = function_templates[x.instantiation - 1];
            const function_template q = function_templates[y.instantiation - 1];
            return p.parameter_count == q.parameter_count &&
                   same_template_parameters(
                       p.first_parameter, q.first_parameter, p.parameter_count);
        }
        return true;
    };
    uint32_t j = 0;
    for (uint32_t i = 0; i < wanted.parameter_count; i++) {
        if (template_parameter_is_pack(wanted.first_parameter + i)) {
            while (j < given.parameter_count && same_form(i, j))
                j++;
            continue;
        }
        if (j >= given.parameter_count || !same_form(i, j))
            return false;
        if (!template_parameter_is_pack(given.first_parameter + j))
            j++;
    }
    for (; j < given.parameter_count; j++) {
        if (template_parameter_is_pack(given.first_parameter + j))
            continue;
        template_parameter_parts form;
        if (!template_parameter_form(
                template_parameter_nodes[given.first_parameter + j], form) ||
            !form.has_default)
            return false;
    }
    return true;
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
    SyntaxRef at, entity_index parameter, SyntaxElement written,
    template_argument &out)
{
    /* the written name: identifier, or nested-name-specifier identifier */
    SyntaxElement name = written;
    SyntaxRef qualifier = 0;
    if (name.is(syntax_kind::type_id)) {
        if (name.size() != 1 || !name[0].is(syntax_kind::type_specifier_seq))
            return not_supported(at, "temp.arg.template");
        name = name[0];
        if (name.size() != 1)
            return not_supported(at, "temp.arg.template");
        name = name[0];
        if (name.is(syntax_kind::simple_type_specifier)) {
            auto c = name;
            if (name.size() != 2 ||
                !c[0].is(syntax_kind::nested_name_specifier))
                return not_supported(at, "temp.arg.template");
            qualifier = c[0].index();
            name = c[1];
        }
    } else if (name.is(syntax_kind::qualified_id)) {
        auto c = name;
        if (name.size() != 2 || !c[0].is(syntax_kind::nested_name_specifier))
            return not_supported(at, "temp.arg.template");
        qualifier = c[0].index();
        name = c[1];
    }
    if (!name.is_token() || tokens[name.token()].kind != token_kind::identifier)
        return not_supported(at, "temp.arg.template");
    lookup_result x;
    if (qualifier) {
        nominated_scope scope;
        VTRY(r, nested_name_specifier(qualifier, scope));
        if (scope.entity && !is_namespace(scope.entity))
            return not_supported(at, "temp.arg.template");
        x = lookup_qualified(scope.entity, tokens.identifier(name.token()),
                             lookup_filter::any);
    } else {
        x = lookup_unqualified(tokens.identifier(name.token()),
                               lookup_filter::any);
    }
    entity_index named = x.entity;
    if (!named)
        named = x.unknown_entity;
    /* (an injected-class-name names its class template here,
       [temp.local]/1) */
    uint32_t a = class_template_of(named);
    if (!a)
        a = alias_template_of(named);
    if (!a || function_templates[a - 1].primary)
        return not_supported(at, "temp.arg.template");
    const function_template given = function_templates[a - 1];
    const function_template wanted =
        function_templates[entity_table[parameter].instantiation - 1];
    if (!template_parameters_match(wanted, given))
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
    SyntaxRef at, uint32_t template_index,
    owned_array<template_argument> &arguments, const owned_array<bool> &set)
{
    const function_template ft = function_templates[template_index - 1];
    entity_index t = ft.entity;
    entity_index saved = current_scope;
    token_index saved_horizon = lookup_horizon;
    for (uint32_t i = 0; i < ft.parameter_count; i++) {
        if (set[i])
            continue;
        SyntaxRef node = template_parameter_nodes[ft.first_parameter + i];
        template_parameter_parts form;
        if (!template_parameter_form(node, form) || !form.has_default)
            return not_supported(at, "temp.arg");
        entity_index scope = entity_table.make(entity_kind::block_scope, 0,
                                               entity_table[t].enclosing);
        /* the names of the declaration that gave the default
           ([temp.param]/12, merged: they may differ from the
           definition's) */
        uint32_t names =
            template_parameter_default_names[ft.first_parameter + i];
        for (uint32_t k = 0; k < i; k++)
            bind_template_argument(
                scope, template_parameter_entities[names + k], arguments[k]);
        current_scope = scope;
        lookup_horizon = ft.declaration.first_token();
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
