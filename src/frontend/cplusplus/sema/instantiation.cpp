/*
 * instantiation.cpp: an instantiation as a recorded unit
 * (doc/PLAN-cxx-sema.md 3.5).
 *
 * [temp.inst]/2: "the class template specialization is implicitly
 * instantiated when the specialization is referenced in a context that
 * requires a completely-defined object type".  Each instantiation is to be
 * recorded with its key (the template and its arguments) and everything it
 * read from outside itself (lookups, overload sets, the specializations
 * visible): semantic_analysis::instantiations and instantiation_inputs.
 * vccd's instantiation cache, a later step, checks that record before it
 * reuses a result.  No template is instantiated yet.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [temp.inst]/5: "Unless a function template specialization is a declared
 * specialization, the function template specialization is implicitly
 * instantiated when the specialization is referenced in a context that
 * requires a function definition to exist or if the existence of the
 * definition affects the semantics of the program."  A call names a
 * specialization: its declaration is instantiated there (the
 * template's declarator analysed again in a scope where each
 * template-parameter's name is a typedef-name of its argument, which
 * declare() makes a function of the template's scope without binding its
 * name, declare_specialization).  Its definition, unless the call is in an
 * unevaluated operand or a default argument: at once for a constexpr
 * template, which a constant expression may evaluate; else after the
 * translation unit's declarations ([temp.point]/7), as clang does.  Not
 * yet the rules of [temp.res]/8 (two-phase lookup): the definition's names
 * are looked up again (lookup_horizon).
 */
analysis_result
semantic_analysis::instantiate(node_index at, entity_index template_entity,
                               const template_argument *arguments,
                               uint32_t count, entity_index &specialization)
{
    specialization = 0;
    const entity t = entity_table[template_entity];
    if (t.kind != entity_kind::template_entity || !t.instantiation)
        return not_supported(at, "temp.inst");
    const function_template ft = function_templates[t.instantiation - 1];
    if (count != ft.parameter_count)
        return not_supported(at, "temp.inst");
    bool needed = !unevaluated;
    uint32_t index = 0;
    uint32_t prior = find_instantiation(template_entity, arguments, count);
    if (prior)
        index = prior - 1;
    if (!prior) {
        index = record_instantiation(ft, arguments, count);
        /* the declaration: the declarator, where its types are the
           arguments' */
        const syntax_child *d = tree.children(ft.declarator);
        entity_index saved_scope = current_scope;
        uint32_t saved_cv = function_cv;
        owned_array<parameter_meaning> saved_parameters =
            static_cast<owned_array<parameter_meaning> &&>(function_parameters);
        size_t expressions_before = expressions.size();
        size_t uses_before = name_uses.size();
        current_scope = instantiations[index].scope;
        /* (a pack expansion's pattern that names the template: its own
           packs are not the pattern's) */
        bool saved_expanding = expanding;
        expanding = false;
        specified s;
        declarator_meaning m;
        analysis_result r = decl_specifier_seq(d[0].index(), s, false);
        qualified_type type = s.type;
        if (r == analysis_result::well_formed) {
            function_cv = 0;
            r = declarator(ft.declarator, d[1], type, m);
        }
        entity_index e = 0;
        if (r == analysis_result::well_formed) {
            instantiating = index + 1;
            r = declare_specialization(ft.declarator, m.id, type, s, e);
            instantiating = 0;
        }
        current_scope = saved_scope;
        expanding = saved_expanding;
        function_cv = saved_cv;
        function_parameters =
            static_cast<owned_array<parameter_meaning> &&>(saved_parameters);
        expressions.resize(expressions_before);
        name_uses.resize(uses_before);
        if (r == analysis_result::ill_formed && substituting) {
            /* [temp.deduct]/8: "If a substitution results in an invalid
               type or expression, type deduction fails." (the caller
               takes the diagnostics back) */
            instantiations[index].substitution_failed = true;
            return r;
        }
        if (r != analysis_result::well_formed)
            /* (an argument that makes the declaration ill-formed is a
               deduction failure, [temp.deduct]/8: no candidate) */
            return not_supported(at, "temp.deduct");
    }
    if (instantiations[index].substitution_failed) {
        if (substituting)
            return analysis_result::ill_formed;
        return not_supported(at, "temp.deduct");
    }
    specialization = instantiations[index].specialization;
    if (!specialization)
        return not_supported(at, "temp.inst");
    instantiation_record &x = instantiations[index];
    if (!needed || x.defined || x.pending || x.defining)
        return analysis_result::well_formed;
    /* (a template declared, not yet defined: its definition may come
       later, [temp.point]/7) */
    if (!t.constexpr_ || !ft.definition) {
        x.pending = true;
        pending_definitions.append(index);
        return analysis_result::well_formed;
    }
    return instantiate_definition(at, index);
}

/*
 * The definition of a specialization: the template's function-definition
 * analysed again in the specialization's scope, declare() finding the
 * function its declaration made.  The declaration under analysis, if any,
 * waits, its state kept aside.  [temp.res]/8's rules broken by the
 * definition are the template's: its declaration is ill-formed (clang
 * reports them there, with a note at the use), or unsupported if analysis
 * has no rules for something in it.
 */
analysis_result semantic_analysis::instantiate_definition(node_index at,
                                                          uint32_t index)
{
    /* [temp.inst]/17: "There is an implementation-defined quantity that
       specifies the limit on the total depth of recursive
       instantiations"; [implimits]/2 suggests 1024, this one is lower
       while every level is a C++ stack frame of analysis */
    if (instantiation_depth >= 64)
        return not_supported(at, "temp.inst");
    entity_index template_entity = instantiations[index].template_entity;
    const function_template ft =
        function_templates[entity_table[template_entity].instantiation - 1];
    size_t unsupported_before = unsupported.size();
    /* (leave_instantiation drops the claims of the template's expressions:
       they are about one specialization, which clang makes none of) */
    instantiation_context context;
    enter_instantiation(context, instantiations[index].scope, ft.declaration);
    expanding = false;
    instantiating = index + 1;
    instantiations[index].defining = true;
    instantiations[index].pending = false;
    analysis_result r = function_definition(ft.definition);
    instantiations[index].defining = false;
    leave_instantiation(context);
    instantiations[index].defined = true;
    entity_table[instantiations[index].specialization].defined = true;
    if (r == analysis_result::well_formed)
        return r;
    for (analysed_declaration &d : declarations) {
        if (d.node != ft.declaration || d.result == analysis_result::ill_formed)
            continue;
        d.result = r;
        if (r == analysis_result::unsupported)
            d.first_unsupported = static_cast<uint32_t>(unsupported_before);
    }
    /* the use names a function all the same */
    return analysis_result::well_formed;
}

/*
 * [temp.inst]/2: "Unless a class template specialization is a declared
 * specialization, the class template specialization is implicitly
 * instantiated when the specialization is referenced in a context that
 * requires a completely-defined object type or when the completeness of
 * the class type affects the semantics of the program."  Here at any use
 * of the template-id: the template's class-specifier analysed again, the
 * class made in the template's namespace without binding its name
 * (class_definition, class_instantiating), the template-parameters' names
 * and the injected-class-name ([class.pre]/2) bound in it.  Its member
 * functions defined in it are analysed with it (/4 would instantiate a
 * member's definition only when used).
 */
analysis_result
semantic_analysis::instantiate_class(node_index at, uint32_t class_template,
                                     const template_argument *arguments,
                                     uint32_t count, entity_index &c)
{
    c = 0;
    const function_template ft = function_templates[class_template - 1];
    if (uint32_t prior = find_instantiation(ft.entity, arguments, count)) {
        uint32_t k = prior - 1;
        const instantiation_record &x = instantiations[k];
        c = x.specialization;
        if (!c || entity_table[c].kind != entity_kind::class_type)
            return not_supported(at, "temp.inst");
        /* a specialization named before its template was defined
           ([temp.inst]/2's "declared specialization" is not one): defined
           now that the template is */
        if (!entity_table[c].defined && !x.defining && !x.pattern &&
            ft.definition)
            return define_class_outside(at, k, c);
        return analysis_result::well_formed;
    }
    if (instantiation_depth >= 64)
        return not_supported(at, "temp.inst");
    /* [temp.spec]/8: "If a function declaration acquired its function type
       through a dependent type without using the syntactic form of a
       function declarator, the program is ill-formed": a member `T f;`
       is a data member's syntax; which declarations name a parameter is
       not kept, so a function type argument is not instantiated */
    for (uint32_t i = 0; i < count; i++) {
        if (type_table[arguments[i].type.unqualified()].kind ==
            type_kind::function)
            return not_supported(at, "temp.spec");
        if (arguments[i].kind == template_argument::kind_t::pack)
            for (uint32_t k = 0; k < arguments[i].count; k++)
                if (type_table[pack_arguments[arguments[i].value + k]
                                   .type.unqualified()]
                        .kind == type_kind::function)
                    return not_supported(at, "temp.spec");
    }
    instantiation_record record;
    record.template_entity = ft.entity;
    /* [temp.class.spec.match]: the definition instantiated, the
       primary's or a specialization's */
    uint32_t pattern = 0;
    owned_array<template_argument> deduced;
    analysis_result matched = matching_specialization(
        at, class_template, arguments, count, pattern, deduced);
    if (matched != analysis_result::well_formed)
        return matched;
    record.pattern = pattern;
    record.first_argument = static_cast<uint32_t>(template_arguments.size());
    record.argument_count = count;
    for (uint32_t i = 0; i < count; i++)
        template_arguments.append(arguments[i]);
    record.first_deduced = static_cast<uint32_t>(template_arguments.size());
    for (const template_argument &a : deduced)
        template_arguments.append(a);
    uint32_t index = static_cast<uint32_t>(instantiations.size());
    instantiations.append(record);
    if (!pattern && !ft.definition) {
        /* [temp.class]/1's template declared, not yet defined: the
           specialization is an incomplete class ([basic.types]/5), which
           a later use defines */
        entity_index scope = entity_table[ft.entity].enclosing;
        c = entity_table.make(entity_kind::class_type,
                              entity_table[ft.entity].name, scope);
        entity_table[c].linkage_of_name =
            entity_table[ft.entity].linkage_of_name;
        entity_table[c].type = qualified_type::make(
            type_table.of_entity(type_kind::class_type, c), cv_none);
        entity_table[c].instantiation = index + 1;
        instantiations[index].specialization = c;
        return analysis_result::well_formed;
    }
    return define_class_outside(at, index, c);
}

/* [temp.deduct]/8: "Only invalid types and expressions in the immediate
   context of the function type, its template parameter types, and its
   explicit-specifier can result in a deduction failure": a class
   instantiated while a candidate's declaration is is not in it */
analysis_result semantic_analysis::define_class_outside(node_index at,
                                                        uint32_t index,
                                                        entity_index &c)
{
    unsigned saved = substituting;
    substituting = 0;
    analysis_result r = define_class_instantiation(at, index, c);
    substituting = saved;
    if (saved && r != analysis_result::well_formed)
        definition_failures++;
    return r;
}

/*
 * The class of instantiations[index], defined: its template's (or partial
 * specialization's) class-specifier analysed again, then the member
 * functions defined outside the class template ([temp.mem.func]/1) with
 * the class's arguments.
 */
analysis_result semantic_analysis::define_class_instantiation(node_index at,
                                                              uint32_t index,
                                                              entity_index &c)
{
    c = 0;
    /* (within a pack expansion's pattern: the class's names are not its) */
    struct expanding_restore {
        bool &flag;
        bool saved;
        ~expanding_restore()
        {
            flag = saved;
        }
    } restore_expanding = {expanding, expanding};
    expanding = false;
    if (instantiation_depth >= 64)
        return not_supported(at, "temp.inst");
    const instantiation_record record = instantiations[index];
    const function_template ft =
        function_templates[entity_table[record.template_entity].instantiation -
                           1];
    function_template definition = ft;
    if (record.pattern)
        definition = function_templates[record.pattern - 1];
    instantiations[index].defining = true;
    size_t unsupported_before = unsupported.size();
    instantiation_context context;
    enter_instantiation(context, entity_table[ft.entity].enclosing,
                        definition.declaration);
    declaration_access = 0;
    instantiating = 0;
    class_instantiating = index + 1;
    type_index type = 0;
    analysis_result r = class_specifier(definition.definition, type);
    leave_instantiation(context);
    instantiations[index].defining = false;
    c = instantiations[index].specialization;
    if (r == analysis_result::well_formed && c &&
        entity_table[c].kind == entity_kind::class_type)
        for (uint32_t m = 0; m < template_member_definitions.size(); m++)
            if (template_member_definitions[m].template_entity ==
                    record.template_entity &&
                template_member_definitions[m].pattern == record.pattern)
                define_template_member(m, index, true);
    if (context.defining_class) {
        /* a class being defined named the specialization: its members so
           far again after the specialization's, one range of
           class_members, as after a nested class (class_definition) */
        entity &o = entity_table[context.defining_class];
        if (o.first_member + o.member_count != class_members.size()) {
            uint32_t first = static_cast<uint32_t>(class_members.size());
            for (uint32_t i = 0; i < o.member_count; i++)
                class_members.append(class_members[o.first_member + i]);
            entity_table[context.defining_class].first_member = first;
        }
    }
    if (r == analysis_result::well_formed && c)
        return r;
    /* the class's rules broken, or not known: [temp.inst]/1 makes the
       instantiation, not the template, ill-formed (clang reports it at the
       use, "in instantiation of"); the use cannot go on without the class
       and is not judged */
    (void)unsupported_before;
    c = 0;
    return not_supported(at, "temp.inst");
}

/* [temp.point]/7: the definitions of the specializations used, after the
   translation unit's declarations (each may use more) */
void semantic_analysis::instantiate_pending()
{
    for (size_t i = 0; i < pending_definitions.size(); i++) {
        uint32_t index = pending_definitions[i];
        if (!instantiations[index].pending)
            continue;
        entity_index t = instantiations[index].template_entity;
        const function_template ft =
            function_templates[entity_table[t].instantiation - 1];
        /* a template never defined: the specialization is not either
           ([temp.inst]/5 needs a definition only to exist) */
        if (!ft.definition) {
            instantiations[index].pending = false;
            continue;
        }
        instantiate_definition(ft.declaration, index);
    }
    pending_definitions.clear();
}

/*
 * declare()'s part when the declarator is a specialization's
 * (instantiate): a function of the template's scope, with the template's
 * linkage ([basic.link]/3-5), its name not bound (a lookup of it finds the
 * template).  [basic.def.odr]/13 lets every translation unit that uses it
 * define it, as an inline function's: it is emitted where used, weak.
 * The template-parameters' names are bound in the function, so that its
 * body finds them (its parameter scope's enclosing scope is the function).
 */
analysis_result semantic_analysis::declare_specialization(
    node_index declaration, token_index id, qualified_type type,
    const specified &s, entity_index &declared_entity)
{
    uint32_t index = instantiating - 1;
    instantiating = 0;
    instantiation_record &record = instantiations[index];
    if (record.specialization) {
        /* the definition of a specialization whose declaration was
           instantiated before */
        declared_entity = record.specialization;
        return analysis_result::well_formed;
    }
    /* (a copy: making the entity can move the table) */
    const entity t = entity_table[record.template_entity];
    if (s.inline_ != t.inline_ || s.constexpr_ != t.constexpr_)
        return not_supported(declaration, "temp.inst");
    entity_index e =
        entity_table.make(entity_kind::function, t.name, t.enclosing);
    entity_table[e].type = type;
    entity_table[e].linkage_of_name = t.linkage_of_name;
    if (type_without_external_linkage(type))
        entity_table[e].linkage_of_name = linkage::internal;
    entity_table[e].inline_ = true;
    entity_table[e].constexpr_ = t.constexpr_;
    entity_table[e].instantiation = index + 1;
    entity_table.add_declaration(e, declaration);
    const function_template ft = function_templates[t.instantiation - 1];
    for (uint32_t i = 0; i < ft.parameter_count; i++) {
        uint32_t name =
            entity_table[template_parameter_entities[ft.first_parameter + i]]
                .name;
        entity_table.bind(e, name, entity_table.find(current_scope, name));
    }
    record.specialization = e;
    declared_name d;
    d.declaration = declaration;
    d.outermost = current_declaration;
    d.entity = e;
    /* (clang's specialization is declared where its template is defined,
       which a template declared before is not yet: no name to claim) */
    d.id = id;
    if (!ft.definition)
        d.id = 0;
    d.kind = entity_kind::function;
    d.type = type;
    d.claimable = false;
    declared.append(d);
    declared_entity = e;
    return analysis_result::well_formed;
}

/*
 * declare()'s part for the variable of a variable template's
 * specialization (instantiate_variable): of the template's namespace, its
 * name not bound there, inline ([temp.inst]/7's implicit instantiation,
 * which clang emits linkonce_odr where used).
 */
analysis_result semantic_analysis::declare_variable_specialization(
    node_index declaration, qualified_type type, const specified &s,
    entity_index &declared_entity)
{
    uint32_t index = variable_instantiating - 1;
    variable_instantiating = 0;
    /* (a copy: making the entity can move the table) */
    const entity t = entity_table[instantiations[index].template_entity];
    if (s.constexpr_ != t.constexpr_ || s.static_ || s.extern_ || s.typedef_)
        return not_supported(declaration, "temp.inst");
    entity_index e =
        entity_table.make(entity_kind::variable, t.name, t.enclosing);
    entity_table[e].type = type;
    entity_table[e].linkage_of_name = t.linkage_of_name;
    if (type_without_external_linkage(type))
        entity_table[e].linkage_of_name = linkage::internal;
    entity_table[e].inline_ = true;
    entity_table[e].constexpr_ = s.constexpr_;
    entity_table[e].defined = true;
    entity_table[e].instantiation = index + 1;
    entity_table.add_declaration(e, declaration);
    instantiations[index].specialization = e;
    declared_name d;
    d.declaration = declaration;
    d.outermost = current_declaration;
    d.entity = e;
    d.id = 0;
    d.kind = entity_kind::variable;
    d.type = type;
    d.claimable = false;
    declared.append(d);
    declared_entity = e;
    return analysis_result::well_formed;
}

bool semantic_analysis::specialization_value(entity_index e, uint32_t i,
                                             uint64_t &magnitude,
                                             bool &negative,
                                             uint32_t &parameter,
                                             uint32_t element) const
{
    const template_argument &a =
        specialization_template_argument(e, i, element);
    magnitude = a.value;
    negative = false;
    parameter = 0;
    if (a.kind != template_argument::kind_t::value)
        return false;
    if (a.template_entity) {
        parameter = static_cast<uint32_t>(a.value) + 1;
        return true;
    }
    if (fundamental_type_is_signed(tokens.files.behavior,
                                   fundamental_of(a.type)) &&
        static_cast<int64_t>(a.value) < 0) {
        negative = true;
        magnitude = 0 - a.value;
    }
    return true;
}

bool semantic_analysis::specialization_template_name(entity_index e, uint32_t i,
                                                     entity_index &named,
                                                     uint32_t &parameter,
                                                     uint32_t element) const
{
    const template_argument &a =
        specialization_template_argument(e, i, element);
    named = 0;
    parameter = 0;
    if (a.kind != template_argument::kind_t::template_name)
        return false;
    named = static_cast<entity_index>(a.value);
    if (a.template_entity) {
        named = a.template_entity;
        parameter = static_cast<uint32_t>(a.value) + 1;
    }
    return true;
}

bool semantic_analysis::specialization_address(entity_index e, uint32_t i,
                                               entity_index &object,
                                               bool &reference,
                                               uint32_t element) const
{
    const template_argument &a =
        specialization_template_argument(e, i, element);
    object = 0;
    reference = false;
    if (a.kind != template_argument::kind_t::value || a.template_entity ||
        !a.value || !address_parameter_type(a.type))
        return false;
    object = static_cast<entity_index>(a.value);
    reference =
        type_table[a.type.unqualified()].kind == type_kind::lvalue_reference;
    return true;
}

/*
 * The analysis's state around one instantiation ([temp.inst]): what the
 * template's declaration is analysed in (scope, declaration's tokens as the
 * lookup horizon, no class being defined, no function, no condition or
 * unevaluated operand) and, on leaving, the state of the use again, the
 * claims of the template's expressions dropped.  The caller sets which
 * instantiation is being made (instantiating, class_instantiating,
 * variable_instantiating) and anything else between the two.
 */
void semantic_analysis::enter_instantiation(instantiation_context &c,
                                            entity_index scope,
                                            node_index declaration)
{
    c.scope = current_scope;
    c.defining_class = defining_class;
    c.last_declared = last_declared;
    c.member_scope = template_member_scope;
    c.declaration = current_declaration;
    c.horizon = lookup_horizon;
    c.callee = overloaded_callee;
    c.function_cv = function_cv;
    c.instantiating = instantiating;
    c.class_instantiating = class_instantiating;
    c.variable_instantiating = variable_instantiating;
    c.unevaluated = unevaluated;
    c.access = declaration_access;
    c.in_condition = in_condition;
    c.claimable = declarations_claimable;
    c.expanding = expanding;
    c.function = static_cast<function_context &&>(function_state);
    function_state = function_context();
    c.parameters =
        static_cast<owned_array<parameter_meaning> &&>(function_parameters);
    c.expressions_before = expressions.size();
    c.uses_before = name_uses.size();
    current_scope = scope;
    current_declaration = declaration;
    lookup_horizon = tree[declaration].first_token;
    defining_class = 0;
    in_condition = false;
    overloaded_callee = 0;
    declarations_claimable = false;
    unevaluated = 0;
    instantiation_depth++;
}

void semantic_analysis::leave_instantiation(instantiation_context &c)
{
    instantiation_depth--;
    current_scope = c.scope;
    defining_class = c.defining_class;
    last_declared = c.last_declared;
    template_member_scope = c.member_scope;
    current_declaration = c.declaration;
    lookup_horizon = c.horizon;
    overloaded_callee = c.callee;
    function_cv = c.function_cv;
    instantiating = c.instantiating;
    class_instantiating = c.class_instantiating;
    variable_instantiating = c.variable_instantiating;
    unevaluated = c.unevaluated;
    declaration_access = c.access;
    in_condition = c.in_condition;
    declarations_claimable = c.claimable;
    expanding = c.expanding;
    function_state = static_cast<function_context &&>(c.function);
    function_parameters =
        static_cast<owned_array<parameter_meaning> &&>(c.parameters);
    expressions.resize(c.expressions_before);
    name_uses.resize(c.uses_before);
}

} // namespace cplusplus
