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
    bool found = false;
    for (uint32_t k = 0; k < instantiations.size() && !found; k++) {
        const instantiation_record &x = instantiations[k];
        if (x.template_entity != template_entity || x.argument_count != count)
            continue;
        bool same = true;
        for (uint32_t i = 0; i < count && same; i++)
            same = template_arguments[x.first_argument + i].type ==
                   arguments[i].type;
        if (same) {
            index = k;
            found = true;
        }
    }
    if (!found) {
        instantiation_record record;
        record.template_entity = template_entity;
        record.first_argument =
            static_cast<uint32_t>(template_arguments.size());
        record.argument_count = count;
        for (uint32_t i = 0; i < count; i++)
            template_arguments.append(arguments[i]);
        /* the template-parameters' names, each a typedef-name of its
           argument ([temp.arg.type]) */
        record.scope =
            entity_table.make(entity_kind::block_scope, 0, t.enclosing);
        for (uint32_t i = 0; i < count; i++) {
            entity_index p =
                template_parameter_entities[ft.first_parameter + i];
            entity_index e = entity_table.make(
                entity_kind::typedef_name, entity_table[p].name, record.scope);
            entity_table[e].type = arguments[i].type;
            entity_table[e].defined = true;
            entity_table.bind(record.scope, entity_table[p].name, e);
        }
        index = static_cast<uint32_t>(instantiations.size());
        instantiations.append(record);
        /* the declaration: the declarator, where its types are the
           arguments' */
        const syntax_child *d = tree.children(ft.definition);
        entity_index saved_scope = current_scope;
        uint32_t saved_cv = function_cv;
        owned_array<parameter_meaning> saved_parameters =
            static_cast<owned_array<parameter_meaning> &&>(function_parameters);
        size_t expressions_before = expressions.size();
        size_t uses_before = name_uses.size();
        current_scope = instantiations[index].scope;
        specified s;
        declarator_meaning m;
        analysis_result r = decl_specifier_seq(d[0].index(), s, false);
        qualified_type type = s.type;
        if (r == analysis_result::well_formed) {
            function_cv = 0;
            r = declarator(ft.definition, d[1], type, m);
        }
        entity_index e = 0;
        if (r == analysis_result::well_formed) {
            instantiating = index + 1;
            r = declare_specialization(ft.definition, m.id, type, s, e);
            instantiating = 0;
        }
        current_scope = saved_scope;
        function_cv = saved_cv;
        function_parameters =
            static_cast<owned_array<parameter_meaning> &&>(saved_parameters);
        expressions.resize(expressions_before);
        name_uses.resize(uses_before);
        if (r != analysis_result::well_formed)
            /* (an argument that makes the declaration ill-formed is a
               deduction failure, [temp.deduct]/8: no candidate) */
            return not_supported(at, "temp.deduct");
    }
    specialization = instantiations[index].specialization;
    if (!specialization)
        return not_supported(at, "temp.inst");
    instantiation_record &x = instantiations[index];
    if (!needed || x.defined || x.pending || x.defining)
        return analysis_result::well_formed;
    if (!t.constexpr_) {
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
    entity_index saved_scope = current_scope;
    entity_index saved_class = defining_class;
    entity_index saved_last = last_declared;
    node_index saved_declaration = current_declaration;
    uint32_t saved_cv = function_cv;
    bool saved_condition = in_condition;
    token_index saved_callee = overloaded_callee;
    bool saved_claimable = declarations_claimable;
    uint32_t saved_instantiating = instantiating;
    token_index saved_horizon = lookup_horizon;
    unsigned saved_unevaluated = unevaluated;
    function_context saved_function =
        static_cast<function_context &&>(function_state);
    function_state = function_context();
    owned_array<parameter_meaning> saved_parameters =
        static_cast<owned_array<parameter_meaning> &&>(function_parameters);
    size_t expressions_before = expressions.size();
    size_t uses_before = name_uses.size();
    size_t unsupported_before = unsupported.size();
    current_scope = instantiations[index].scope;
    current_declaration = ft.declaration;
    defining_class = 0;
    in_condition = false;
    overloaded_callee = 0;
    declarations_claimable = false;
    unevaluated = 0;
    instantiating = index + 1;
    lookup_horizon = tree[ft.declaration].first_token;
    instantiations[index].defining = true;
    instantiations[index].pending = false;
    instantiation_depth++;
    analysis_result r = function_definition(ft.definition);
    instantiation_depth--;
    instantiations[index].defining = false;
    unevaluated = saved_unevaluated;
    lookup_horizon = saved_horizon;
    instantiating = saved_instantiating;
    declarations_claimable = saved_claimable;
    overloaded_callee = saved_callee;
    in_condition = saved_condition;
    function_cv = saved_cv;
    last_declared = saved_last;
    current_declaration = saved_declaration;
    defining_class = saved_class;
    current_scope = saved_scope;
    function_state = static_cast<function_context &&>(saved_function);
    function_parameters =
        static_cast<owned_array<parameter_meaning> &&>(saved_parameters);
    /* the claims of the template's expressions are about one
       specialization, which clang makes none of */
    expressions.resize(expressions_before);
    name_uses.resize(uses_before);
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

/* [temp.point]/7: the definitions of the specializations used, after the
   translation unit's declarations (each may use more) */
void semantic_analysis::instantiate_pending()
{
    for (size_t i = 0; i < pending_definitions.size(); i++) {
        uint32_t index = pending_definitions[i];
        if (!instantiations[index].pending)
            continue;
        entity_index t = instantiations[index].template_entity;
        instantiate_definition(
            function_templates[entity_table[t].instantiation - 1].declaration,
            index);
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
    d.id = id;
    d.kind = entity_kind::function;
    d.type = type;
    d.claimable = false;
    declared.append(d);
    declared_entity = e;
    return analysis_result::well_formed;
}

} // namespace cplusplus
