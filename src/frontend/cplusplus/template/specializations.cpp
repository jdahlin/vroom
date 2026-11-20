/*
 * specializations.cpp: explicit specializations ([temp.expl.spec]) and
 * explicit instantiations ([temp.explicit]) of function, class and member
 * templates, and the records of the specializations instantiated
 * (find_instantiation, record_instantiation).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* [temp.expl.spec]/1: "An explicit specialization of any of the following
   ... can be declared by a declaration introduced by template<>" */
analysis_result
semantic_analysis::explicit_specialization(ExplicitSpecialization n)
{
    /* template < > declaration: a class's definition, its class-head
       a simple-template-id */
    if (n.size() == 4 && n[3].is(syntax_kind::simple_declaration)) {
        SyntaxRef only = sole_decl_specifier(n[3]);
        if (only && only.is(syntax_kind::class_specifier)) {
            owned_array<SyntaxRef> none;
            return class_specialization_definition(n, only, none);
        }
        /* template<> class-key simple-template-id ; declared only */
        auto d = n[3];
        if (only && only.is(syntax_kind::elaborated_type_specifier) &&
            d.size() == 2 && only.size() == 2 && only[0].is_token() &&
            only[1].is(syntax_kind::simple_template_id) &&
            d[1].is(preprocessing_op_or_punc::SEMICOLON) &&
            !leading_gnu_attributes(d)) {
            standard_keyword key = only[0].keyword();
            if (key == standard_keyword::struct_ ||
                key == standard_keyword::class_) {
                owned_array<SyntaxRef> none;
                return class_specialization_definition(n, only, none);
            }
        }
    }
    /* ... or a function template's specialization, declared or defined */
    if (n.size() != 4 || n[3].is_token())
        return not_supported(n, "temp.expl.spec");
    auto inner = n[3];
    entity_index qualifier = 0;
    if (SyntaxRef id = variable_specialization_id(inner))
        return variable_specialization(n, inner, id, qualifier);
    if (SyntaxRef id = qualified_variable_specialization_id(inner, qualifier))
        return variable_specialization(n, inner, id, qualifier);
    if (!inner.is(syntax_kind::function_definition,
                  syntax_kind::simple_declaration))
        return not_supported(n, "temp.expl.spec");
    bool defines = inner.is(syntax_kind::function_definition);
    /* [dcl.fct.def.delete], = default: not yet */
    if (defines && !inner.function_definition().function_body().plain())
        return not_supported(n, "temp.expl.spec");
    specified s;
    uint32_t index = 0;
    VTRY(r, named_specialization(n, inner, true, false, s, index));
    /* [temp.expl.spec]/14 (Note): the template's exception specification
       is instantiated for the comparison: not here */
    if (dependent_exception_specification(
            instantiations[index].template_entity))
        return not_supported(n, "except.spec");
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
analysis_result
semantic_analysis::explicit_instantiation(ExplicitInstantiation n)
{
    /* [extern] template declaration */
    bool extern_ = n.size() == 3;
    if ((n.size() != 2 && n.size() != 3) || n[n.size() - 1].is_token())
        return not_supported(n, "temp.explicit");
    /* [temp.explicit]/3: "An explicit instantiation shall appear in an
       enclosing namespace of its template": here, its own */
    if (!is_namespace(current_scope) || defining_class || function_state.active)
        return not_supported(n, "temp.explicit");
    auto d = n[n.size() - 1];
    if (!d.is(syntax_kind::simple_declaration) || leading_gnu_attributes(d))
        return not_supported(n, "temp.explicit");
    if (d.size() == 2 && d[0].is(syntax_kind::decl_specifier_seq) &&
        d[0].size() == 1 &&
        d[0][0].is(syntax_kind::elaborated_type_specifier)) {
        /* [temp.explicit]/3: "If the explicit instantiation is for a
           class or member class, the elaborated-type-specifier in the
           declaration shall include a simple-template-id": class-key
           simple-template-id */
        auto e = d[0][0];
        auto ec = e;
        if (e.size() != 2 || !ec[0].is_token() ||
            !ec[1].is(syntax_kind::simple_template_id) ||
            (ec[0].keyword() != standard_keyword::struct_ &&
             ec[0].keyword() != standard_keyword::class_))
            return not_supported(n, "temp.explicit");
        return class_explicit_instantiation(n, ec[1], extern_);
    }
    if (d.size() != 3 || !d[0].is(syntax_kind::decl_specifier_seq) ||
        d[1].is_token() || !d[2].is(preprocessing_op_or_punc::SEMICOLON))
        return not_supported(n, "temp.explicit");
    /* a member function of a class template's specialization:
       S<A>::f(...) */
    if (d[1].is(syntax_kind::noptr_declarator) && d[1].size() &&
        d[1][0].is(syntax_kind::qualified_id))
        return member_explicit_instantiation(n, d, extern_);
    specified s;
    uint32_t index = 0;
    VTRY(r, named_specialization(n, d, false, extern_, s, index));
    entity_index e = instantiations[index].specialization;
    if (extern_)
        entity_table[e].explicitly_instantiated =
            instantiation_kind::declaration;
    else
        entity_table[e].explicitly_instantiated =
            instantiation_kind::definition;
    return analysis_result::well_formed;
}

uint32_t semantic_analysis::find_instantiation(entity_index t,
                                               const template_argument *a,
                                               uint32_t count) const
{
    for (uint32_t k = 0; k < instantiations.size(); k++) {
        const instantiation_record &x = instantiations[k];
        if (x.template_entity != t || x.argument_count != count)
            continue;
        bool same = true;
        for (uint32_t i = 0; i < x.argument_count && same; i++)
            same = template_arguments[x.first_argument + i] == a[i];
        if (same)
            return k + 1;
    }
    return 0;
}

uint32_t
semantic_analysis::record_instantiation(const function_template &ft,
                                        const template_argument *arguments,
                                        uint32_t count)
{
    instantiation_record record;
    record.template_entity = ft.entity;
    record.first_argument = static_cast<uint32_t>(template_arguments.size());
    record.argument_count = count;
    for (uint32_t i = 0; i < count; i++)
        template_arguments.append(arguments[i]);
    record.scope = entity_table.make(entity_kind::block_scope, 0,
                                     entity_table[ft.entity].enclosing);
    for (uint32_t i = 0; i < count; i++)
        bind_template_argument(
            record.scope, template_parameter_entities[ft.first_parameter + i],
            arguments[i]);
    uint32_t index = static_cast<uint32_t>(instantiations.size());
    instantiations.append(record);
    return index;
}

/* the simple-template-id a simple-declaration of one declarator with an
   initializer declares (the declarator-id of a variable template's
   explicit specialization), or 0 */
SyntaxRef semantic_analysis::variable_specialization_id(SyntaxRef d) const
{
    if (!d.is(syntax_kind::simple_declaration) || d.size() != 3 ||
        !d[1].is(syntax_kind::init_declarator))
        return 0;
    auto item = d[1];
    if (item.size() != 2 || !item[0].is(syntax_kind::simple_template_id))
        return 0;
    auto id = item[0];
    lookup_result x;
    entity_index named = 0;
    if (!template_id_name(id, x, named) || x.ambiguous ||
        !variable_template_of(named))
        return 0;
    return id;
}

/* [temp.expl.spec]/1, [dcl.meaning]/1: the same with a qualified
   template-id, N::name<args>, its qualifier a namespace the template is a
   member of ([namespace.qual]/2); qualifier is that namespace */
SyntaxRef
semantic_analysis::qualified_variable_specialization_id(SyntaxRef d,
                                                        entity_index &qualifier)
{
    qualifier = 0;
    if (!d.is(syntax_kind::simple_declaration) || d.size() != 3 ||
        !d[1].is(syntax_kind::init_declarator))
        return 0;
    auto item = d[1];
    if (item.size() != 2 || !item[0].qualified_id())
        return 0;
    auto q = item[0].qualified_id();
    if (q.size() != 2 || !q.nested_name_specifier() ||
        !q[1].is(syntax_kind::simple_template_id) || !q[1][0].is_identifier())
        return 0;
    declarator_meaning m;
    if (declarator_qualifier(q.nested_name_specifier(), m) !=
            analysis_result::well_formed ||
        !m.qualified_namespace)
        return 0;
    lookup_result x = lookup_in_namespace(m.namespace_qualifier,
                                          tokens.identifier(q[1][0].token()),
                                          lookup_filter::any);
    entity_index named = x.entity;
    if (!named)
        named = x.unknown_entity;
    if (x.ambiguous || !variable_template_of(named))
        return 0;
    /* [temp.expl.spec]/2: declared in a namespace that encloses the
       template's (clang's error otherwise) */
    entity_index scope = m.namespace_qualifier;
    while (scope != current_scope && scope)
        scope = entity_table[scope].enclosing;
    if (scope != current_scope)
        return 0;
    qualifier = m.namespace_qualifier;
    return q[1];
}

/*
 * [temp.expl.spec]/1: a variable template's explicit specialization
 * template<> T name<args> = init;: the specialization's variable, declared
 * by this declaration as an instantiation declares one by the template's
 * (declare_variable_specialization), which no instantiation makes then;
 * inline only if declared so ([temp.expl.spec]/12).  One the program used
 * before is not analysed ([temp.expl.spec]/7: clang's error).
 */
analysis_result
semantic_analysis::variable_specialization(SyntaxRef n, SyntaxRef inner,
                                           SyntaxRef id, entity_index qualifier)
{
    lookup_result x;
    entity_index named = 0;
    if (qualifier) {
        x = lookup_in_namespace(qualifier, tokens.identifier(id[0].token()),
                                lookup_filter::any);
        named = x.entity;
        if (!named)
            named = x.unknown_entity;
    } else {
        template_id_name(id, x, named);
    }
    uint32_t vt = variable_template_of(named);
    owned_array<template_argument> arguments;
    declaring_specializations++;
    analysis_result r;
    if (qualifier)
        r = template_id_arguments_in(id, vt, qualifier, arguments);
    else
        r = template_id_arguments(id, vt, arguments);
    declaring_specializations--;
    if (r != analysis_result::well_formed)
        return r;
    for (const template_argument &a : arguments)
        if (a.kind == template_argument::kind_t::pack ||
            depends_on_template_parameter(a.type) || a.template_entity)
            return not_supported(n, "temp.expl.spec");
    const function_template ft = function_templates[vt - 1];
    uint32_t count = static_cast<uint32_t>(arguments.size());
    if (count != ft.parameter_count ||
        find_instantiation(ft.entity, arguments.data(), count))
        return not_supported(n, "temp.expl.spec");
    uint32_t index = record_instantiation(ft, arguments.data(), count);
    instantiations[index].explicit_specialization = true;
    variable_instantiating = index + 1;
    declarator_template_id = true;
    r = simple_declaration(inner);
    declarator_template_id = false;
    variable_instantiating = 0;
    if (r != analysis_result::well_formed)
        return r;
    if (!instantiations[index].specialization)
        return not_supported(n, "temp.expl.spec");
    instantiations[index].defined = true;
    return analysis_result::well_formed;
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
semantic_analysis::named_specialization(SyntaxRef n, SyntaxRef declaration,
                                        bool specialization, bool extern_,
                                        specified &s, uint32_t &index)
{
    index = 0;
    const char *rule = "temp.explicit";
    if (specialization)
        rule = "temp.expl.spec";
    if (leading_gnu_attributes(declaration) || declaration.size() != 3 ||
        !declaration[0].is(syntax_kind::decl_specifier_seq) ||
        declaration[1].is_token() ||
        (!declaration[1].is(syntax_kind::noptr_declarator,
                            syntax_kind::ptr_declarator)))
        return not_supported(n, rule);
    if (declaration.is(syntax_kind::function_definition) &&
        !declaration.function_definition().plain())
        return not_supported(n, rule);
    if (declaration.is(syntax_kind::simple_declaration) &&
        !declaration[2].is(preprocessing_op_or_punc::SEMICOLON))
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
    analysis_result r = decl_specifier_seq(declaration[0], s, false);
    uint32_t cv = 0;
    if (r == analysis_result::well_formed) {
        type = s.type;
        function_cv = 0;
        function_parameters.clear();
        declarator_template_id = true;
        r = declarator(declaration, declaration[1], type, m);
        declarator_template_id = false;
        cv = function_cv;
        if (r == analysis_result::well_formed)
            r = misplaced_function_qualifiers(
                declaration, type,
                entity_table[current_scope].kind == entity_kind::class_type);
    }
    bool defaults = false;
    for (const parameter_meaning &p : function_parameters)
        if (p.default_argument)
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
        s.aligned || m.aligned || s.section || m.section || m.asm_label)
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
    SyntaxElement callee = SyntaxElement::from_token(m.id);
    if (m.template_id)
        callee = m.template_id;
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
    {
        /* [temp.func.order]/1.4: which of the name's templates the
           declaration refers to is partial ordering's: not analysed */
        owned_array<uint32_t> group;
        function_template_overloads(function_templates[ft - 1].name, group);
        if (group.size() > 1)
            return not_supported(n, rule);
    }
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
    uint32_t prior = find_instantiation(f.entity, values.data(),
                                        static_cast<uint32_t>(values.size()));
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
analysis_result semantic_analysis::class_explicit_instantiation(SyntaxRef n,
                                                                SyntaxRef id,
                                                                bool extern_)
{
    lookup_result x;
    entity_index named = 0;
    if (!template_id_name(id, x, named))
        return not_supported(n, "temp.explicit");
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
    uint32_t prior = find_instantiation(
        t, arguments.data(), static_cast<uint32_t>(arguments.size()));
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
    SyntaxRef n, SyntaxRef declaration, bool extern_)
{
    size_t expressions_before = expressions.size();
    size_t uses_before = name_uses.size();
    entity_index outer = current_scope;
    uint32_t saved_cv = function_cv;
    owned_array<parameter_meaning> saved_parameters =
        static_cast<owned_array<parameter_meaning> &&>(function_parameters);
    specified s;
    declarator_meaning m;
    qualified_type type;
    analysis_result r = decl_specifier_seq(declaration[0], s, false);
    uint32_t cv = 0;
    if (r == analysis_result::well_formed) {
        type = s.type;
        function_cv = 0;
        function_parameters.clear();
        r = declarator(declaration, declaration[1], type, m);
        cv = function_cv;
        if (r == analysis_result::well_formed)
            r = misplaced_function_qualifiers(
                declaration, type,
                entity_table[current_scope].kind == entity_kind::class_type);
    }
    bool defaults = false;
    for (const parameter_meaning &p : function_parameters)
        if (p.default_argument)
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
        m.asm_label ||
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
    /* [ext:gnu_attributes] exclude_from_explicit_instantiation: a member
       an explicit instantiation of its class leaves implicit */
    if (x.gnu_flags & gnu_exclude_from_explicit_instantiation)
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

} // namespace cplusplus
