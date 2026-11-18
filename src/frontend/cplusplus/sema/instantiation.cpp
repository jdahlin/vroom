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
 * [temp.inst]/1 for a function template: "the implicit instantiation of a
 * class template specialization causes ... the implicit instantiation of
 * the declarations, but not of the definitions" ... /11: "An
 * implementation shall not implicitly instantiate a function template ...
 * unless such instantiation is required."  A specialization is
 * instantiated where a call names it: the template's function-definition
 * analysed again in a scope where each template-parameter's name is a
 * typedef-name of its argument, which declare() makes a function of the
 * template's scope without binding its name (declare_specialization).
 * The analysis of the declaration it is met in waits for it, with that
 * declaration's state kept aside.  Not yet the rules of [temp.res]/8
 * (two-phase lookup): a name the body uses is looked up at the point of
 * instantiation.
 */
analysis_result
semantic_analysis::instantiate(node_index at, entity_index template_entity,
                               const template_argument *arguments,
                               uint32_t count, entity_index &specialization)
{
    specialization = 0;
    for (const instantiation_record &x : instantiations) {
        if (x.template_entity != template_entity || x.argument_count != count)
            continue;
        bool same = true;
        for (uint32_t i = 0; i < count && same; i++)
            same = template_arguments[x.first_argument + i].type ==
                   arguments[i].type;
        if (!same)
            continue;
        /* one whose declarator is being analysed has no function yet */
        if (!x.specialization)
            return not_supported(at, "temp.inst");
        specialization = x.specialization;
        return analysis_result::well_formed;
    }
    /* [temp.inst]/17: "There is an implementation-defined quantity that
       specifies the limit on the total depth of recursive
       instantiations"; [implimits]/2 suggests 1024, this one is lower
       while every level is a C++ stack frame of analysis */
    if (instantiation_depth >= 64)
        return not_supported(at, "temp.inst");
    const entity t = entity_table[template_entity];
    if (t.kind != entity_kind::template_entity || !t.instantiation)
        return not_supported(at, "temp.inst");
    const function_template ft = function_templates[t.instantiation - 1];
    if (count != ft.parameter_count)
        return not_supported(at, "temp.inst");
    instantiation_record record;
    record.template_entity = template_entity;
    record.first_argument = static_cast<uint32_t>(template_arguments.size());
    record.argument_count = count;
    for (uint32_t i = 0; i < count; i++)
        template_arguments.append(arguments[i]);
    uint32_t index = static_cast<uint32_t>(instantiations.size());
    instantiations.append(record);
    /* the template-parameters' names, each a typedef-name of its argument
       ([temp.arg.type]) */
    entity_index scope =
        entity_table.make(entity_kind::block_scope, 0, t.enclosing);
    for (uint32_t i = 0; i < count; i++) {
        entity_index p = template_parameter_entities[ft.first_parameter + i];
        entity_index e = entity_table.make(entity_kind::typedef_name,
                                           entity_table[p].name, scope);
        entity_table[e].type = arguments[i].type;
        entity_table[e].defined = true;
        entity_table.bind(scope, entity_table[p].name, e);
    }
    /* the declaration under analysis: its state, kept aside */
    entity_index saved_scope = current_scope;
    entity_index saved_class = defining_class;
    entity_index saved_last = last_declared;
    uint32_t saved_cv = function_cv;
    bool saved_condition = in_condition;
    token_index saved_callee = overloaded_callee;
    bool saved_claimable = declarations_claimable;
    uint32_t saved_instantiating = instantiating;
    token_index saved_horizon = lookup_horizon;
    function_context saved_function =
        static_cast<function_context &&>(function_state);
    function_state = function_context();
    owned_array<parameter_meaning> saved_parameters =
        static_cast<owned_array<parameter_meaning> &&>(function_parameters);
    size_t expressions_before = expressions.size();
    size_t uses_before = name_uses.size();
    current_scope = scope;
    defining_class = 0;
    in_condition = false;
    overloaded_callee = 0;
    declarations_claimable = false;
    instantiating = index + 1;
    lookup_horizon = tree[ft.declaration].first_token;
    instantiation_depth++;
    analysis_result r = function_definition(ft.definition);
    instantiation_depth--;
    lookup_horizon = saved_horizon;
    instantiating = saved_instantiating;
    declarations_claimable = saved_claimable;
    overloaded_callee = saved_callee;
    in_condition = saved_condition;
    function_cv = saved_cv;
    last_declared = saved_last;
    defining_class = saved_class;
    current_scope = saved_scope;
    function_state = static_cast<function_context &&>(saved_function);
    function_parameters =
        static_cast<owned_array<parameter_meaning> &&>(saved_parameters);
    /* the claims of the template's expressions are about one
       specialization, which clang makes none of */
    expressions.resize(expressions_before);
    name_uses.resize(uses_before);
    if (r == analysis_result::ill_formed &&
        instantiations[index].specialization) {
        /* [temp.res]/8's rules broken by the specialization's definition:
           the template's declaration is where clang reports them (with a
           note at the use); the call names a function all the same */
        for (analysed_declaration &d : declarations)
            if (d.node == ft.declaration)
                d.result = analysis_result::ill_formed;
        r = analysis_result::well_formed;
    }
    if (r != analysis_result::well_formed) {
        instantiations[index].specialization = 0;
        return r;
    }
    specialization = instantiations[index].specialization;
    if (!specialization)
        return not_supported(at, "temp.inst");
    return analysis_result::well_formed;
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
    entity_table[e].defined = true;
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
    d.id = id;
    d.kind = entity_kind::function;
    d.type = type;
    d.claimable = false;
    declared.append(d);
    declared_entity = e;
    return analysis_result::well_formed;
}

} // namespace cplusplus
