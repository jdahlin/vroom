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
semantic_analysis::instantiate(SyntaxRef at, entity_index template_entity,
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
        auto d_syntax = ft.declarator;
        uint32_t d_skip = leading_gnu_attributes(ft.declarator);
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
        placeholder_allowed = true;
        /* (a constructor template's: no type, the decl-specifier-seq
           optional, [class.ctor]/1) */
        bool specifiers = d_syntax[d_skip].is(syntax_kind::decl_specifier_seq);
        bool constructor = t.special == special_member::constructor;
        analysis_result r = analysis_result::well_formed;
        bool lambda = ft.declarator.is(syntax_kind::lambda_expression);
        /* [temp.deduct.guide]/1: a deduction guide's function type, its
           parameters returning the class template's specialization */
        bool guide = ft.guide_of != 0;
        qualified_type lambda_type;
        if (guide)
            r = deduction_guide_type(ft.declarator, lambda_type);
        else if (lambda)
            r = lambda_specialization(ft.declarator, false, lambda_type, 0);
        else if (constructor && specifiers)
            r = constructor_specifiers(d_syntax[d_skip], s);
        else if (specifiers)
            r = decl_specifier_seq(d_syntax[d_skip], s, false);
        qualified_type type = s.type;
        if (guide)
            type = lambda_type;
        if (lambda) {
            type = lambda_type;
            s.constexpr_ = t.constexpr_;
        }
        if (r == analysis_result::well_formed && !lambda && !guide) {
            function_cv = 0;
            /* (a member function template's cv-qualifier-seq,
               [temp.mem]) */
            member_template_declarator(2 * !is_namespace(t.enclosing));
            if (constructor)
                r = constructor_template_declarator(
                    ft.declarator, d_syntax[d_skip + specifiers], type, m);
            else
                r = declarator(ft.declarator, d_syntax[d_skip + specifiers],
                               type, m);
            member_template_declarator(0);
        }
        entity_index e = 0;
        if (r == analysis_result::well_formed) {
            instantiating = index + 1;
            r = declare_specialization(ft.declarator, m.id, type, s, e);
            instantiating = 0;
        }
        /* [dcl.fct.def.delete]/2: a specialization of a deleted template
           is deleted from its declaration on, a call of it ill-formed;
           its definition is the deleted definition */
        if (r == analysis_result::well_formed && ft.definition && !lambda &&
            deleted_body(ft.definition.back())) {
            entity_table[e].deleted = true;
            entity_table[e].inline_ = true;
            instantiations[index].defined = true;
        }
        /* [temp.inst]/12: "If a function template f is called in a way
           that requires a default argument to be used, the dependent names
           are looked up, the semantics constraints are checked, and the
           instantiation of any template used in the default argument is
           done as if the default argument had been an initializer used in
           a function template specialization"; here for each
           specialization, one that is not well-formed left to a call that
           uses it */
        if (r == analysis_result::well_formed && ft.default_arguments) {
            size_t unsupported_before = unsupported.size();
            if (default_arguments(ft.declarator, e, entity_kind::function,
                                  true) != analysis_result::well_formed) {
                unsupported.resize(unsupported_before);
                entity_table[e].unknown_defaults = true;
            }
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
    /* [dcl.spec.auto]/12: "Return type deduction for a templated function
       with a placeholder in its declared type occurs when the definition
       is instantiated even if the function body contains a return
       statement with a non-type-dependent operand"; a use of a
       specialization whose return type is not deduced yet instantiates
       its definition there, in an unevaluated operand too */
    bool deducing = contains_placeholder(
        type_table[entity_table[specialization].type.unqualified()].operand);
    if (x.defined || x.defining || (!deducing && (!needed || x.pending)))
        return analysis_result::well_formed;
    /* (a template declared, not yet defined: its definition may come
       later, [temp.point]/7) */
    if ((!t.constexpr_ && !deducing) || !ft.definition) {
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
analysis_result semantic_analysis::instantiate_definition(SyntaxRef at,
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
    /* a member function template's definition, in its class
       ([temp.mem]): its declarator's cv-qualifier-seq the member's */
    member_template_declarator(
        2 * !is_namespace(entity_table[template_entity].enclosing));
    /* (defined outside its class, [temp.mem]/1: its qualified declarator-id
       is at the scope of the template-parameters' names) */
    if (ft.definition != ft.declarator)
        template_member_scope = instantiations[index].scope;
    analysis_result r = analysis_result::well_formed;
    if (entity_table[template_entity].special == special_member::constructor) {
        /* a constructor template's ([temp.mem]/1): its declarator again
           (the parameters), then its function-body */
        auto d_syntax = ft.definition;
        uint32_t d_skip = leading_gnu_attributes(ft.definition);
        bool specifiers = d_syntax[d_skip].is(syntax_kind::decl_specifier_seq);
        qualified_type type;
        declarator_meaning m;
        function_cv = 0;
        function_parameters.clear();
        r = constructor_template_declarator(
            ft.definition, d_syntax[d_skip + specifiers], type, m);
        last_declared = instantiations[index].specialization;
        if (r == analysis_result::well_formed)
            r = special_member_body(
                d_syntax, d_syntax[d_skip + specifiers + 1 +
                                   (static_cast<bool>(
                                       trailing_constraint(ft.declaration)))]
                              .function_body());
    } else if (ft.definition.is(syntax_kind::lambda_expression)) {
        /* a generic lambda's ([expr.prim.lambda.closure]/3) */
        qualified_type type;
        r = lambda_specialization(ft.definition, true, type,
                                  instantiations[index].specialization);
    } else {
        r = function_definition(ft.definition);
    }
    member_template_declarator(0);
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
        d.has_unsupported = unsupported.size() > unsupported_before;
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
semantic_analysis::instantiate_class(SyntaxRef at, uint32_t class_template,
                                     const template_argument *arguments,
                                     uint32_t count, entity_index &c)
{
    c = 0;
    const function_template ft = function_templates[class_template - 1];
    if (uint32_t prior = find_instantiation(ft.entity, arguments, count)) {
        uint32_t k = prior - 1;
        const instantiation_record x = instantiations[k];
        c = x.specialization;
        if (!c || entity_table[c].kind != entity_kind::class_type) {
            /* the definition failed earlier, perhaps where the failure was
               dropped (a substitution, a default argument): the use reports
               what went wrong there, not a refusal of its own */
            if (x.has_failure) {
                unsupported.append(x.failure);
                return analysis_result::unsupported;
            }
            return not_supported(at, "temp.inst");
        }
        /* a specialization named before its template was defined
           ([temp.inst]/2's "declared specialization" is not one): defined
           now that the template is */
        if (entity_table[c].defined || x.defining)
            return analysis_result::well_formed;
        /* the pattern again: a specialization declared since it was
           named ([temp.class.spec.match]) */
        uint32_t pattern = 0;
        owned_array<template_argument> deduced;
        VTRY(matched, matching_specialization(at, class_template, arguments,
                                              count, pattern, deduced));
        if (pattern != x.pattern) {
            instantiation_record &y = instantiations[k];
            y.pattern = pattern;
            y.first_deduced = static_cast<uint32_t>(template_arguments.size());
            for (const template_argument &a : deduced)
                template_arguments.append(a);
        }
        SyntaxRef defined_by = ft.definition;
        if (pattern)
            defined_by = function_templates[pattern - 1].definition;
        if (defined_by && !declaring_specializations)
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
    VTRY(matched, matching_specialization(at, class_template, arguments, count,
                                          pattern, deduced));
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
    SyntaxRef defined_by = ft.definition;
    if (pattern)
        defined_by = function_templates[pattern - 1].definition;
    if (!defined_by || declaring_specializations) {
        /* [temp.class]/1's template, or [temp.expl.spec]/1's explicit
           specialization, declared, not yet defined: the specialization
           is an incomplete class ([basic.types]/5), which a later use
           defines.  So is one named in a specialization's head (an argument,
           not a nested-name-specifier's class), which
           needs no completely-defined type ([temp.inst]/2; libc++'s
           __desugars_to_v<__plus_tag, plus<void>, ...> before
           plus<void>'s explicit specialization, [temp.expl.spec]/7) */
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
analysis_result semantic_analysis::define_class_outside(SyntaxRef at,
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
analysis_result semantic_analysis::define_class_instantiation(SyntaxRef at,
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
    postpone_member_bodies = context.defining_class != 0;
    type_index type = 0;
    analysis_result r = class_specifier(definition.definition, type);
    postpone_member_bodies = false;
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
    /* a class being defined named the specialization: its members so far
       again after the specialization's, as after a nested class
       (class_definition) */
    if (context.defining_class)
        make_members_contiguous(context.defining_class);
    if (r == analysis_result::well_formed && c)
        return r;
    /* the class's rules broken, or not known: [temp.inst]/1 makes the
       instantiation, not the template, ill-formed (clang reports it at the
       use, "in instantiation of"); the use cannot go on without the class
       and is not judged */
    if (unsupported.size() > unsupported_before) {
        instantiations[index].failure = unsupported[unsupported_before];
        instantiations[index].has_failure = true;
    }
    c = 0;
    return not_supported(at, "temp.inst");
}

/* [temp.point]/7: the definitions of the specializations used, after the
   translation unit's declarations (each may use more) */
void semantic_analysis::instantiate_pending()
{
    for (size_t i = 0; i < postponed_bodies.size(); i++) {
        const postponed_body b = postponed_bodies[i];
        if (!b.c)
            continue;
        instantiation_context context;
        enter_instantiation(context, b.scope, b.declaration);
        instantiated_member_body(b.c, b.body);
        leave_instantiation(context);
    }
    postponed_bodies.clear();
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
    SyntaxRef declaration, token_index id, qualified_type type,
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
    /* (a member function template is inline, [class.mfct]/1; the
       definition's declarator may leave out the inline of an earlier
       declaration, [dcl.inline]/5) */
    bool inline_ = s.inline_ || !is_namespace(t.enclosing);
    if ((inline_ && !t.inline_) || s.constexpr_ != t.constexpr_)
        return not_supported(declaration, "temp.inst");
    entity_index e =
        entity_table.make(entity_kind::function, t.name, t.enclosing);
    entity_table[e].type = type;
    entity_table[e].linkage_of_name = t.linkage_of_name;
    if (type_without_external_linkage(type))
        entity_table[e].linkage_of_name = linkage::internal;
    entity_table[e].inline_ = true;
    entity_table[e].constexpr_ = t.constexpr_;
    entity_table[e].immediate = t.immediate;
    entity_table[e].operator_function = t.operator_function;
    entity_table[e].implemented = t.implemented;
    entity_table[e].instantiation = index + 1;
    /* a member function template's specialization: a member function of
       its class ([temp.mem]) */
    entity_table[e].nonstatic_member = t.nonstatic_member;
    entity_table[e].member_cv = t.member_cv;
    entity_table[e].access = t.access;
    entity_table[e].special = t.special;
    /* [dcl.fct.spec]/2: a dependent condition is the specialization's own */
    entity_table[e].explicit_ = t.explicit_;
    if (t.explicit_dependent)
        entity_table[e].explicit_ = s.explicit_;
    entity_table.add_declaration(e, declaration);
    const function_template ft = function_templates[t.instantiation - 1];
    entity_table[e].gnu_flags = ft.gnu_flags;
    entity_table[e].abi_tag = ft.abi_tag;
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
    SyntaxRef declaration, qualified_type type, const specified &s,
    entity_index &declared_entity)
{
    uint32_t index = variable_instantiating - 1;
    variable_instantiating = 0;
    /* (a copy: making the entity can move the table) */
    const entity t = entity_table[instantiations[index].template_entity];
    bool explicit_ = instantiations[index].explicit_specialization;
    if ((s.constexpr_ != t.constexpr_ && !explicit_) || s.static_ ||
        s.extern_ || s.typedef_)
        return not_supported(declaration, "temp.inst");
    entity_index e =
        entity_table.make(entity_kind::variable, t.name, t.enclosing);
    entity_table[e].type = type;
    entity_table[e].linkage_of_name = t.linkage_of_name;
    if (type_without_external_linkage(type))
        entity_table[e].linkage_of_name = linkage::internal;
    /* ([temp.expl.spec]/12: an explicit specialization's as declared) */
    entity_table[e].inline_ = !explicit_ || s.inline_;
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
    /* (a value-dependent expression: not mangled yet) */
    if (a.template_entity &&
        entity_table[a.template_entity].kind == entity_kind::dependent_value)
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

bool semantic_analysis::specialization_dependent_value(entity_index e,
                                                       uint32_t i,
                                                       uint32_t element) const
{
    const template_argument &a =
        specialization_template_argument(e, i, element);
    return a.kind == template_argument::kind_t::value && a.template_entity &&
           entity_table[a.template_entity].kind == entity_kind::dependent_value;
}

bool semantic_analysis::dependent_parameter_type(entity_index t, uint32_t i,
                                                 qualified_type &type) const
{
    for (const function_template &ft : function_templates) {
        if (ft.entity != t || i >= ft.parameter_count)
            continue;
        const entity &p =
            entity_table[template_parameter_entities[ft.first_parameter + i]];
        if (p.kind != entity_kind::none ||
            !depends_on_template_parameter(p.type))
            return false;
        type = p.type;
        return true;
    }
    return false;
}

token_index semantic_analysis::parameter_type_constraint(entity_index t,
                                                         uint32_t i) const
{
    for (const function_template &ft : function_templates) {
        if (ft.entity != t || i >= ft.parameter_count)
            continue;
        template_parameter_parts x;
        if (!template_parameter_form(
                template_parameter_nodes[ft.first_parameter + i], x) ||
            !x.constraint || x.constraint.size() != 1 ||
            !x.constraint[0].is_token())
            return 0;
        return x.constraint[0].token();
    }
    return 0;
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
                                            SyntaxRef declaration)
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
    lookup_horizon = declaration.first_token();
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
