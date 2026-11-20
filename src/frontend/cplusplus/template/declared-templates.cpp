/*
 * declared-templates.cpp: the semantic side of [temp.pre] and
 * [temp.deduct.guide]: the declarations of templates (function, class, alias,
 * variable) and of class templates' partial specializations and members.
 * Explicit specializations and instantiations are in specializations.cpp,
 * template arguments in template-argument.cpp, deduction in
 * argument-deduction.cpp.  (The grammar is the parser's, in
 * template-declaration.cpp and concept-definition.cpp.)
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
analysis_result semantic_analysis::template_declaration(TemplateDeclaration t)
{
    /* [temp.concept]/1: "A concept is a template that defines constraints
       on its template arguments." */
    if (auto c = t.concept_definition())
        return concept_definition(t, c);
    return t.declaration().visit(Overloaded{
        /* [temp.fct]/1: "A function template defines an unbounded set of
           related functions." */
        [&](FunctionDefinition f) {
            /* [temp.mem.func]/1: "A member function of a class template
               may be defined outside of the class template definition in
               which it is declared." */
            if (out_of_class_member(f))
                return member_definition_of_template(t, f);
            return function_template_definition(t, f);
        },
        /* [temp.mem]/1: "A member template can be defined within or
           outside its class definition or class template definition"
           (template<...> template<...> ...) */
        [&](TemplateDeclaration inner) {
            /* [temp.mem.func]/1's member template of a class template,
               defined outside it */
            SyntaxRef d = inner.declaration();
            if (d.is(syntax_kind::function_definition) &&
                out_of_class_member(d))
                return member_definition_of_template(t, d, inner);
            return not_supported(t, "temp.mem");
        },
        /* [temp.alias]/1: "A template-declaration in which the declaration
           is an alias-declaration declares the identifier to be an alias
           template." */
        [&](AliasDeclaration a) { return alias_template_definition(t, a); },
        [&](SimpleDeclaration s) { return template_simple_declaration(t, s); },
        [&](DeductionGuide g) { return deduction_guide(g); },
        /* (or no declaration: a token) */
        [&](Declaration) { return not_supported(t, "temp.pre"); }});
}

/* a template-declaration whose declaration is a simple-declaration */
analysis_result
semantic_analysis::template_simple_declaration(TemplateDeclaration t,
                                               SimpleDeclaration s)
{
    /* a function template's declaration or a variable template of several
       declarators: which needs the declarator's type */
    if (s.init_declarator_list().is(syntax_kind::init_declarator_list))
        return not_supported(t, "temp.pre");
    /* [temp.class]/1: "A class template defines the layout and operations
       for an unbounded set of related types." (a declaration with no
       declarator: a class's) */
    SyntaxRef only = sole_decl_specifier(s);
    if (only && only.is(syntax_kind::class_specifier))
        return class_template_definition(t, only);
    /* [temp.class]/1's declaration without a definition (template<class T>
       struct S;) */
    if (only && only.is(syntax_kind::elaborated_type_specifier))
        return class_template_declaration(t, only);
    /* [temp.pre]/1: a variable template, its one init-declarator with an
       initializer, after attributes without effect (libc++'s deprecated
       is_pod_v) */
    uint32_t lead = attributes_without_effect_before(s);
    if (s.size() == 3 + lead &&
        s.is(s[lead], syntax_kind::decl_specifier_seq) &&
        s.is(s[lead + 1], syntax_kind::init_declarator)) {
        /* (a specialization's template-id after attributes: no rules
           yet) */
        auto item = s[lead + 1];
        if (lead && item.is(item[0], syntax_kind::simple_template_id))
            return not_supported(t, "temp.pre");
        if (SyntaxRef id = variable_specialization_id(s))
            return variable_partial_specialization(t, s, id);
        return variable_template_definition(t, s);
    }
    /* [temp.fct]/1: a function template's declaration (template<class T>
       T f(T);), one declarator not in an init-declarator-list */
    uint32_t gnu = leading_gnu_attributes(s);
    if (s.size() == 3 + gnu &&
        (s.is(s[1 + gnu], syntax_kind::noptr_declarator) ||
         s.is(s[1 + gnu], syntax_kind::ptr_declarator)))
        return function_template_definition(t, s);
    return not_supported(t, "temp.class");
}

/* [temp.deduct.guide]/1: "Deduction guides are used when a template-name
   appears as a type specifier for a deduced class type" */
analysis_result semantic_analysis::deduction_guide(DeductionGuide n)
{
    /* the template-name: the token before the parameters' '(' */
    token_index name = 0;
    SyntaxRef id = 0;
    for (uint32_t i = 1; i < n.size(); i++) {
        SyntaxElement c = n[i];
        if (!name && c.is(preprocessing_op_or_punc::LEFT_PARENTHESIS))
            name = n[i - 1].token();
        if (c.is(syntax_kind::simple_template_id))
            id = c.index();
    }
    /* [temp.deduct.guide]/3: "The template-name shall be the same
       identifier as the template-name of the simple-template-id." */
    if (!id || tokens.identifier(id.first_token()) != tokens.identifier(name))
        return ill_formed_at_token(name,
                                   "the template-name shall be the same "
                                   "identifier as the template-name of the "
                                   "simple-template-id",
                                   "temp.deduct.guide");
    /* [temp.deduct.guide]/1: a guide is no function and gets no code; it
       is used only by class template argument deduction
       ([over.match.class.deduct]), which is not implemented: a
       placeholder for a deduced class type stays refused. */
    return analysis_result::well_formed;
}

/*
 * [temp.pre]/1: the template-head's template-parameter-list: type
 * parameters of the form class identifier (typename identifier), with no
 * default, pack or type-constraint; a requires-clause is [temp.constr].
 * The template is one of namespace scope.
 */
/* [temp.pre]/1: a template-head's requires-clause's constraint-expression,
   or none */
SyntaxElement semantic_analysis::head_constraint(SyntaxRef n) const
{
    if (n.is(syntax_kind::lambda_expression))
        return lambda_constraint(n);
    if (!n.size() || !n[0].is(syntax_kind::template_head))
        return SyntaxElement();
    auto head = n[0];
    for (SyntaxElement c : head.elements()) {
        if (c.is(syntax_kind::requires_clause) && c.size() == 2)
            return c[1];
    }
    return SyntaxElement();
}

/* [dcl.decl]/4: a function template's trailing requires-clause, the
   function-definition's child before its function-body, or none */
SyntaxElement semantic_analysis::trailing_constraint(SyntaxRef n) const
{
    if (n.size() != 2 || n[1].is_token())
        return SyntaxElement();
    auto d = n[1];
    if (!d.is(syntax_kind::function_definition))
        return SyntaxElement();
    for (SyntaxElement c : d.elements()) {
        if (c.is(syntax_kind::requires_clause) && c.size() == 2)
            return c[1];
    }
    return SyntaxElement();
}

analysis_result semantic_analysis::template_head_parameters(
    SyntaxRef n, owned_array<SyntaxRef> &parameters, bool packs,
    bool constrained, bool pack_anywhere, bool type_constraints)
{
    /* [dcl.fct]/18: an abbreviated function template without a
       template-head, its parameters all invented */
    if (n.is(syntax_kind::function_definition)) {
        if (!abbreviated_parameters(n, parameters) || parameters.empty())
            return not_supported(n, "temp.variadic");
        return analysis_result::well_formed;
    }
    if (n.size() != 2 || !n[0].is(syntax_kind::template_head))
        return not_supported(n, "temp.pre");
    /* a member alias template of a class of namespace scope
       (member_template_declaration) is the one member template here */
    bool member_alias =
        entity_table[current_scope].kind == entity_kind::class_type &&
        is_namespace(entity_table[current_scope].enclosing) &&
        n[1].is(syntax_kind::alias_declaration);
    /* ... and a member class template (member_template_declaration) */
    bool member_class =
        entity_table[current_scope].kind == entity_kind::class_type &&
        n[1].is(syntax_kind::member_declaration);
    if (!member_alias && !member_class &&
        (!is_namespace(current_scope) || defining_class))
        return not_supported(n, "temp.mem");
    /* template-head: template < template-parameter-list >; a
       requires-clause is [temp.constr] */
    auto head = n[0];
    if (!template_head_list(head, parameters))
        return not_supported(n, "temp.pre");
    /* [dcl.fct]/22: the invented template-parameters after the declared */
    if (!abbreviated_parameters(n, parameters))
        return not_supported(n, "temp.variadic");
    if (head_constraint(n) && !constrained)
        return not_supported(n, "temp.constr");
    /* a type-parameter class identifier_opt, a template template-parameter
       template < type-parameters > class identifier_opt, a
       parameter-declaration decl-specifier-seq identifier_opt, each with =
       and a default template-argument or not: a type-constraint or another
       declarator is a rule of its own */
    for (size_t i = 0; i < parameters.size(); i++) {
        SyntaxRef p = parameters[i];
        template_parameter_parts x;
        if (!template_parameter_form(p, x))
            return not_supported(p, "temp.param");
        /* [temp.param]/4: a type-constraint's immediately-declared
           constraint is one of the template's associated constraints
           ([temp.constr.decl]/3), checked with its requires-clause
           (template_constraints_satisfied): a function template's and a
           partial specialization's ([temp.class.spec.match]/2) */
        if (x.constraint && (!type_constraints || x.pack))
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
        owned_array<SyntaxRef> inner;
        if (!template_head_list(x.head, inner) || inner.empty())
            return not_supported(p, "temp.arg.template");
        for (size_t k = 0; k < inner.size(); k++) {
            template_parameter_parts y;
            if (!template_parameter_form(inner[k], y) || y.has_declarator ||
                y.template_template || y.has_default ||
                (y.pack && k + 1 != inner.size()))
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
 * A function template's decl-specifier-seq, after the lead GNU attributes
 * of definition (its specializations'), and declarator, in the
 * template-parameters' scope; what they record for claims is the
 * template's, which clang has no claim for.  member_of: a member function
 * template's class, whose cv-qualifier-seq is the member's ([dcl.fct]/6);
 * a constructor template's declarator ([temp.mem]/1) declares no type
 * ([class.ctor]/4), its decl-specifier-seq optional.
 */
analysis_result semantic_analysis::function_template_declarator(
    SyntaxRef definition, uint32_t lead, entity_index member_of,
    entity_index scope, specified &s, declarator_meaning &m,
    qualified_type &type)
{
    bool specifiers = definition[lead].is(syntax_kind::decl_specifier_seq);
    bool constructor =
        member_of &&
        declares_special_member(member_of, definition[lead + specifiers]);
    size_t expressions_before = expressions.size();
    size_t uses_before = name_uses.size();
    entity_index outer = current_scope;
    current_scope = scope;
    /* [dcl.spec.auto]/3: a declared return type with a placeholder,
       deduced per specialization (instantiate) */
    placeholder_allowed = true;
    analysis_result r = analysis_result::well_formed;
    if (constructor && specifiers)
        r = constructor_specifiers(definition[lead], s);
    else if (specifiers)
        r = decl_specifier_seq(definition[lead], s, false);
    for (uint32_t i = 0; i < lead && r == analysis_result::well_formed; i++)
        r = gnu_attribute_specifier(definition[i], s);
    if (r == analysis_result::well_formed) {
        type = s.type;
        function_cv = 0;
        function_parameters.clear();
        if (member_of)
            member_template_declarator(1);
        if (constructor)
            r = constructor_template_declarator(
                definition, definition[lead + specifiers], type, m);
        else
            r = declarator(definition, definition[lead + specifiers], type, m);
        if (member_of)
            member_template_declarator(0);
    }
    current_scope = outer;
    expressions.resize(expressions_before);
    name_uses.resize(uses_before);
    return r;
}

/*
 * [ext:gnu_attributes] a function template's attributes: its
 * specializations' symbols' (given them at instantiation) and the GNU
 * attributes of allowed, one abi_tag; any other is not analysed
 * (subclause).
 */
analysis_result semantic_analysis::function_template_attributes(
    SyntaxRef n, const specified &s, const declarator_meaning &m,
    uint32_t allowed, const char *subclause, uint32_t &gnu_flags,
    token_index &abi_tag)
{
    gnu_flags = s.gnu_flags | m.gnu_flags;
    if (s.typedef_ || s.extern_ ||
        (gnu_flags & ~(gnu_function_symbol_flags | allowed)) || s.aligned ||
        m.aligned || s.section || m.section || m.asm_label ||
        (s.abi_tag && m.abi_tag && s.abi_tag != m.abi_tag))
        return not_supported(n, subclause);
    abi_tag = m.abi_tag;
    if (!abi_tag)
        abi_tag = s.abi_tag;
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
 * hold_names binds, which a call finds (call, postfix-expression.cpp); any
 * other use of it is [temp.names].
 */
analysis_result
semantic_analysis::function_template_definition(SyntaxRef n,
                                                SyntaxRef definition)
{
    entity_index outer = current_scope;
    owned_array<SyntaxRef> parameters;
    /* [temp.param]/14: a function template's pack may be followed by
       parameters that are deduced or have defaults */
    VTRY(head, template_head_parameters(n, parameters, true, true, true, true));
    /* a function-definition, or a simple-declaration of one declarator
       without an initializer: the template declared, defined later;
       [ext:gnu_attributes] before it, its specializations' */
    bool defines = definition.is(syntax_kind::function_definition);
    uint32_t lead = leading_gnu_attributes(definition);
    SyntaxTail tail(definition, lead);
    /* [dcl.decl]/4: a trailing requires-clause (trailing_constraint),
       where the template-head has none */
    uint32_t trailing = static_cast<bool>(trailing_constraint(n));
    if (trailing && head_constraint(n))
        return not_supported(n, "temp.constr");
    if (tail.size() != 3 + trailing ||
        !tail[0].is(syntax_kind::decl_specifier_seq) || tail[1].is_token() ||
        (defines && !tail[2 + trailing].is(syntax_kind::function_body)) ||
        (!defines && (!tail[2].is(preprocessing_op_or_punc::SEMICOLON) ||
                      (!tail[1].is(syntax_kind::noptr_declarator,
                                   syntax_kind::ptr_declarator)))))
        return not_supported(n, "temp.fct");
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY(declared, template_parameter_scope(n, parameters, outer, 0, index,
                                            scope, first));
    specified s;
    declarator_meaning m;
    qualified_type type;
    VTRY(r,
         function_template_declarator(definition, lead, 0, scope, s, m, type));
    /* an operator function template (libc++'s byte& operator<<=(byte&,
       _Integer)): its name is operator@ ([over.oper]); not new or delete */
    if (!m.has_id || m.qualifier || m.qualified_namespace ||
        m.allocation != allocation_function::none || m.literal_operator ||
        function_cv ||
        type_table[type.unqualified()].kind != type_kind::function)
        return not_supported(n, "temp.fct");
    if (c_language)
        return not_supported(n, "temp.fct");
    uint32_t gnu_flags = 0;
    token_index abi_tag = 0;
    VTRY_SET(r, function_template_attributes(n, s, m, gnu_inline_flags,
                                             "temp.fct", gnu_flags, abi_tag));
    /* [dcl.fct.default]/4: the trailing parameters with a default
       argument */
    uint32_t defaults = 0;
    for (const parameter_meaning &p : function_parameters) {
        if (!p.default_argument)
            defaults = 0;
        else
            defaults++;
    }
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
        const struct type ft = type_table[type.unqualified()];
        const qualified_type *list = type_table.parameters(type.unqualified());
        /* [temp.param]/14: "A template parameter pack of a function
           template shall not be followed by another template parameter
           unless that template parameter can be deduced from the
           parameter-type-list": several packs, each named only in pack
           expansions inside the parameters' types (tuple<A...>,
           seq<N...>), no function parameter pack */
        if (packs > 1) {
            for (uint32_t i = 0; i < ft.parameter_count; i++)
                if (type_table[list[i].unqualified()].kind ==
                    type_kind::pack_expansion)
                    return not_supported(n, "temp.variadic");
            for (uint32_t i = 0; i < parameters.size(); i++)
                if (template_parameter_is_pack(first + i) &&
                    !pack_only_expanded(
                        entity_table[template_parameter_entities[first + i]]
                            .name,
                        definition[lead].first_token(),
                        definition[lead + 1].end_token()))
                    return not_supported(n, "temp.variadic");
            packs = 0;
        }
        bool pack = packs == 1;
        uint32_t pack_name = 0;
        if (pack)
            pack_name =
                entity_table[template_parameter_entities[first + last]].name;
        /* the return type and the parameters' types: the pack named only
           in pack expansions (res<A...>, tuple<A...>&), each name of it
           followed by ... */
        if (pack && depends(type_table, ft.operand, &entity_table, last) &&
            !pack_only_expanded(pack_name, tail[0].first_token(),
                                tail[0].end_token()))
            return not_supported(n, "temp.variadic");
        for (uint32_t i = 0; i < ft.parameter_count; i++) {
            const struct type pt = type_table[list[i].unqualified()];
            if (pt.kind != type_kind::pack_expansion) {
                if (pack && depends(type_table, list[i], &entity_table, last) &&
                    !pack_only_expanded(pack_name, tail[1].first_token(),
                                        tail[1].end_token()))
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
        /* [namespace.udecl]/13: one a using-declaration introduced joins
           the overload set with this one unless they are equivalent */
        if (same && entity_table[f.entity].enclosing != outer)
            return not_supported(n, "namespace.udecl");
        if (same)
            earlier = g;
    }
    if (prior && earlier) {
        const function_template f = function_templates[earlier - 1];
        const entity old = entity_table[f.entity];
        if (f.definition && defines)
            /* [basic.def.odr]/1: a second definition */
            return not_supported(n, "basic.def.odr");
        /* (default arguments of one declaration, which the other's
           declarator the specializations are made from has not) */
        if (defaults || f.default_arguments)
            return not_supported(n, "dcl.fct.default");
        if (old.enclosing != outer || f.parameter_count != parameters.size() ||
            !same_template_parameters(first, f.first_parameter,
                                      f.parameter_count) ||
            old.constexpr_ != s.constexpr_ ||
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
        /* [dcl.inline]/5: an inline declaration after the definition is
           ill-formed; one before it makes the template inline */
        if (s.inline_ && !old.inline_ && f.definition)
            return not_supported(n, "dcl.inline");
        if (s.inline_)
            entity_table[f.entity].inline_ = true;
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
    record.default_arguments = defaults;
    entity_index t = record.entity;
    entity_table[t].type = type;
    entity_table[t].defined = defines;
    entity_table[t].inline_ = s.inline_;
    entity_table[t].constexpr_ = s.constexpr_;
    entity_table[t].immediate = s.consteval_;
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
       scope's own declarations, [namespace.udir]/2, unless no namespace
       the directives nominate declares it or has directives of its own) */
    bool own = !entity_table[outer].unknown_names;
    if (own && entity_table[outer].has_using_directive)
        for (const using_directive_record &u : using_directives)
            if (u.scope == outer &&
                (entity_table[u.nominated].has_using_directive ||
                 entity_table[u.nominated].unknown_names ||
                 entity_table.find(u.nominated, name)))
                own = false;
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
    SyntaxRef n, const owned_array<SyntaxRef> &parameters, entity_index scope,
    uint32_t &index, uint32_t reserved)
{
    owned_array<uint32_t> template_templates;
    uint32_t first = static_cast<uint32_t>(template_parameter_entities.size());
    for (size_t i = 0; i < parameters.size(); i++) {
        SyntaxRef node = parameters[i];
        template_parameter_parts form;
        if (!template_parameter_form(node, form))
            return not_supported(node, "temp.param");
        token_index id = form.id;
        uint32_t name = 0;
        if (form.named)
            name = tokens.identifier(id);
        else if (node.is(syntax_kind::decl_specifier_seq))
            name = invented_parameter_name(node);
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
            analysis_result r = decl_specifier_seq(node[0], sp, false);
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
        owned_array<SyntaxRef> inner;
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
        VTRY(r, declare_template_parameters(n, inner, inner_scope, own, 0));
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

/* the usable function template (member: member function template) whose
   name the held entity is, plus one */
uint32_t semantic_analysis::function_template_of(entity_index held,
                                                 bool member) const
{
    for (size_t i = 0; i < function_templates.size(); i++) {
        const function_template &f = function_templates[i];
        if (f.name == held && held && !f.unusable && !f.is_class &&
            !f.is_alias && !f.is_variable &&
            is_namespace(entity_table[f.entity].enclosing) != member)
            return static_cast<uint32_t>(i + 1);
    }
    return 0;
}

entity_index semantic_analysis::member_templates_of(entity_index e) const
{
    if (!e)
        return 0;
    if (entity_table[e].kind == entity_kind::function)
        return entity_table[e].member_templates;
    if (entity_table[e].kind == entity_kind::none &&
        member_function_template_of(e))
        return e;
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
bool semantic_analysis::template_id_name(SyntaxRef id, lookup_result &x,
                                         entity_index &named) const
{
    if (id.size() < 3 || !id[0].is_identifier())
        return false;
    x = lookup_unqualified(tokens.identifier(id[0].token()),
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
analysis_result semantic_analysis::alias_template_definition(SyntaxRef n,
                                                             SyntaxRef alias)
{
    entity_index outer = current_scope;
    owned_array<SyntaxRef> parameters;
    VTRY(r, template_head_parameters(n, parameters, true, true, false, true));
    uint32_t name = tokens.identifier(alias.first_token() + 1);
    if (entity_index prior = entity_table.find(outer, name)) {
        /* a redeclaration of the same tokens (libc++'s __swap_result_t in
           two headers), as clang accepts it; [temp.alias] has no rule for
           one that differs */
        uint32_t same = alias_template_of(prior);
        if (!same || !same_tokens(function_templates[same - 1].declaration, n))
            return not_supported(n, "temp.alias");
        entity_table.add_declaration(function_templates[same - 1].entity, n);
        return analysis_result::well_formed;
    }
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY_SET(r, template_parameter_scope(n, parameters, outer, name, index,
                                         scope, first));
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
    SyntaxRef n, SyntaxRef declaration, SyntaxRef id)
{
    entity_index outer = current_scope;
    owned_array<SyntaxRef> parameters;
    /* [temp.param]/14: a partial specialization's packs are deduced,
       wherever they are */
    VTRY(r, template_head_parameters(n, parameters, true, true, true, true));
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
    VTRY_SET(r, template_parameter_scope(n, parameters, outer, 0, index, scope,
                                         first));
    owned_array<template_argument> arguments;
    partial_arguments++;
    declaring_specializations++;
    r = template_id_arguments_in(id, vt, scope, arguments);
    partial_arguments--;
    declaring_specializations--;
    if (r != analysis_result::well_formed)
        return r;
    bool dependent = dependent_arguments(arguments);
    bool identity = primary_arguments(n, primary.declaration, arguments, first,
                                      parameters.size());
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
semantic_analysis::variable_template_definition(SyntaxRef n,
                                                SyntaxRef declaration)
{
    entity_index outer = current_scope;
    owned_array<SyntaxRef> parameters;
    VTRY(r, template_head_parameters(n, parameters, true));
    uint32_t skip = attributes_without_effect_before(declaration);
    auto item = declaration[skip + 1];
    if (item.size() != 2 || item[1].is(syntax_kind::requires_clause))
        return not_supported(n, "temp.pre");
    /* (the name is not known before the declarator: the template-parameters
       are checked against it below) */
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY_SET(r, template_parameter_scope(n, parameters, outer, 0, index, scope,
                                         first));
    if (default_missing_after_one(parameters))
        return not_supported(n, "temp.param");
    entity_index saved_scope = current_scope;
    uint32_t saved_cv = function_cv;
    current_scope = scope;
    specified s;
    declarator_meaning m;
    r = decl_specifier_seq(declaration[skip], s, false);
    qualified_type type = s.type;
    if (r == analysis_result::well_formed) {
        function_cv = 0;
        r = declarator(declaration, item[0], type, m);
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
    for (SyntaxElement k : declaration[skip].elements()) {
        if (k.is(syntax_kind::class_specifier,
                 syntax_kind::elaborated_type_specifier,
                 syntax_kind::enum_specifier))
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
analysis_result semantic_analysis::variable_template_id(SyntaxRef n,
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
    VTRY(r, template_id_arguments(n, vt, arguments));
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
    VTRY_SET(r,
             instantiate_variable(n, vt, arguments.data(),
                                  static_cast<uint32_t>(arguments.size()), e));
    size_t uses_before = name_uses.size();
    r = named_entity(n[0].token(), e, m);
    /* (clang's lookup of the name finds the template, no claim here) */
    name_uses.resize(uses_before);
    return r;
}

analysis_result
semantic_analysis::instantiate_variable(SyntaxRef at, uint32_t vt,
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
    VTRY(matched,
         matching_specialization(at, vt, arguments, count, pattern, deduced));
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
analysis_result semantic_analysis::alias_template_type(SyntaxRef id,
                                                       uint32_t at,
                                                       type_index &type,
                                                       uint32_t *cv)
{
    owned_array<template_argument> arguments;
    VTRY(r, template_id_arguments(id, at, arguments));
    return alias_template_specialization(id, at, arguments, type, cv);
}

/* the dependent type template t's simple-template-id with arguments
   denotes ([temp.dep.type]/9), not instantiated; one per template and
   arguments */
type_index semantic_analysis::dependent_specialization(
    entity_index t, const owned_array<template_argument> &arguments)
{
    for (const instantiation_record &x : instantiations) {
        if (x.template_entity != t || x.argument_count != arguments.size())
            continue;
        bool same = true;
        for (uint32_t i = 0; i < x.argument_count && same; i++)
            same = template_arguments[x.first_argument + i] == arguments[i];
        if (same)
            return entity_table[x.specialization].type.unqualified();
    }
    instantiation_record record;
    record.template_entity = t;
    record.first_argument = static_cast<uint32_t>(template_arguments.size());
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
    return entity_table[e].type.unqualified();
}

/* the type alias template function_templates[at - 1] denotes with
   arguments (one per parameter, a pack's as one) */
analysis_result semantic_analysis::alias_template_specialization(
    SyntaxRef id, uint32_t at, const owned_array<template_argument> &arguments,
    type_index &type, uint32_t *cv)
{
    const function_template ft = function_templates[at - 1];
    analysis_result r = analysis_result::well_formed;
    /* an enclosing template's own pack expanded into the alias's pack
       (Numbers... unbound): its elements are not known.  Forwarded whole
       to a pack parameter (libc++'s __invoke_result_t<_Args...>), the
       parameter's name is bound to the enclosing pack itself, so that the
       alias denotes what its type-id with that pack written there would
       ([temp.alias]/2); anything else (__invoke_result_t<_Fn, _Args...>)
       is the dependent template-id itself ([temp.dep.type]/9), analysed
       again with the elements when the enclosing template is
       instantiated ([temp.variadic]/5) */
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
                        entity_kind::typedef_name) {
                    type = dependent_specialization(ft.entity, arguments);
                    return analysis_result::well_formed;
                }
                whole = x.template_entity;
            }
        forwarded.append(whole);
    }
    /* [temp.alias]/2: "when a template-id refers to the specialization of
       an alias template, it is equivalent to the associated type", which
       [temp.names]/7 requires to satisfy the template's associated
       constraints ([temp.constr.decl]/3); of dependent arguments, they
       cannot be checked yet, and the type is the defining-type-id with the
       arguments substituted, itself dependent (clang mangles it so) */
    if (!dependent_arguments(arguments)) {
        bool satisfied = true;
        VTRY_SET(r, template_constraints_satisfied(at, arguments, satisfied));
        /* clang reports the unsatisfied constraint at the use; while a
           candidate is substituted, a deduction failure */
        if (!satisfied && substituting)
            return analysis_result::ill_formed;
        if (!satisfied)
            return not_supported(id, "temp.names");
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
    lookup_horizon = ft.declaration.first_token();
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
    /* a cv-qualified type (alias<const T>) to a caller that takes its cv;
       the others refuse it rather than drop the cv */
    if (t.cv() && !cv)
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
        if (dependent_arguments(arguments))
            return not_supported(id, "temp.alias");
    }
    type = t.unqualified();
    if (cv)
        *cv = t.cv();
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
    case type_kind::bit_int:
        return r.entity &&
               (!parameters || (*parameters)[r.entity].value >= from);
    case type_kind::ext_vector:
        if (r.entity && (!parameters || (*parameters)[r.entity].value >= from))
            return true;
        return depends(table, r.operand, parameters, from);
    case type_kind::array:
        /* T[N]: N a non-type template-parameter */
        if (r.entity && (!parameters || (*parameters)[r.entity].value >= from))
            return true;
        return depends(table, r.operand, parameters, from);
    case type_kind::pointer_to_member:
        /* [temp.dep.type]/9: a pointer to member of a dependent class, or
           to a dependent member type */
        if (!r.entity && depends(table, qualified_type::make(r.bound, cv_none),
                                 parameters, from))
            return true;
        return depends(table, r.operand, parameters, from);
    case type_kind::pointer:
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
    case type_kind::pack_expansion:
    case type_kind::atomic:
    case type_kind::complex:
    case type_kind::block_pointer:
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

/*
 * [temp.class.spec]/9: "The specialization shall be more specialized than
 * the primary template": whether partial specialization n's arguments are
 * its parameters (first, count of them) in order, the primary's own,
 * unless n is constrained and the primary is not (/9 Example 1's #2:
 * more constrained, [temp.constr.order]); other cases are
 * [temp.class.order]'s, not analysed.
 */
bool semantic_analysis::primary_arguments(
    SyntaxRef n, SyntaxRef primary,
    const owned_array<template_argument> &arguments, uint32_t first,
    size_t count)
{
    if (arguments.size() != count)
        return false;
    for (uint32_t i = 0; i < count; i++) {
        entity_index pe = template_parameter_entities[first + i];
        const template_argument &a = arguments[i];
        if (!(a.template_entity == pe ||
              (a.kind == template_argument::kind_t::type &&
               a.type == entity_table[pe].type) ||
              (a.kind == template_argument::kind_t::pack && a.count == 1 &&
               pack_arguments[a.value].count == UINT32_MAX &&
               pack_arguments[a.value].template_entity == pe)))
            return false;
    }
    return !template_head_constrained(n) || template_head_constrained(primary);
}

/* whether a template argument, or a pack's element, is dependent */
bool semantic_analysis::dependent_arguments(
    const owned_array<template_argument> &arguments) const
{
    for (const template_argument &a : arguments) {
        if (depends_on_template_parameter(a.type) || a.template_entity)
            return true;
        if (a.kind == template_argument::kind_t::pack)
            for (uint32_t k = 0; k < a.count; k++)
                if (depends_on_template_parameter(
                        pack_arguments[a.value + k].type) ||
                    pack_arguments[a.value + k].template_entity)
                    return true;
    }
    return false;
}

/* the scope of the template-parameters ([basic.scope.temp]) in outer, the
   parameters declared in it (declare_template_parameters, reserved the
   template's name), the template's record index-to-be and its first
   parameter */
analysis_result semantic_analysis::template_parameter_scope(
    SyntaxRef n, const owned_array<SyntaxRef> &parameters, entity_index outer,
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
    const owned_array<SyntaxRef> &parameters) const
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
    SyntaxRef id, uint32_t ct, entity_index scope,
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
bool semantic_analysis::expansion_list(SyntaxRef list,
                                       owned_array<SyntaxElement> &written,
                                       owned_array<bool> &expansion) const
{
    for (SyntaxElement a : list.elements()) {
        if (a.is(preprocessing_op_or_punc::COMMA))
            continue;
        if (a.is(preprocessing_op_or_punc::ELLIPSIS)) {
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
SyntaxRef semantic_analysis::sole_decl_specifier(SyntaxRef declaration) const
{
    if (declaration.size() != 2 ||
        !declaration[0].is(syntax_kind::decl_specifier_seq))
        return 0;
    auto seq = declaration[0];
    if (seq.size() != 1 || seq[0].is_token())
        return 0;
    return seq[0].index();
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
semantic_analysis::class_template_definition(SyntaxRef n, SyntaxRef specifier)
{
    entity_index outer = current_scope;
    owned_array<SyntaxRef> parameters;
    /* class-head: class-key identifier base-clause_opt */
    if (!specifier.size() || !specifier[0].is(syntax_kind::class_head))
        return not_supported(n, "temp.class");
    auto head = specifier[0];
    SyntaxElement head_name = class_head_name(head);
    bool specialization =
        head_name && head_name.is(syntax_kind::simple_template_id);
    VTRY(r, template_head_parameters(n, parameters, true, true, specialization,
                                     true));
    /* a partial specialization's requires-clause is checked where it is
       matched (matching_specialization); the primary's where a
       specialization is named (template_type) */
    if (specialization) {
        return class_specialization_definition(n, specifier, parameters);
    }
    if (!head_name.is_token() ||
        tokens[head_name.token()].kind != token_kind::identifier)
        return not_supported(n, "temp.class");
    uint32_t name = tokens.identifier(head_name.token());
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY_SET(r, template_parameter_scope(n, parameters, outer, name, index,
                                         scope, first));
    /* the definition of a class template declared before
       (class_template_declaration): its record, defined now */
    entity_index prior = entity_table.find(outer, name);
    uint32_t declared = class_template_of(prior);
    if (prior && declared && function_templates[declared - 1].name == prior &&
        !function_templates[declared - 1].definition &&
        !function_templates[declared - 1].primary) {
        const function_template d = function_templates[declared - 1];
        /* [temp.over.link]/6: the same template-head, its requires-clause
           equivalent (/7) */
        if (entity_table[d.entity].enclosing != outer ||
            d.parameter_count != parameters.size() ||
            !same_template_parameters(first, d.first_parameter,
                                      d.parameter_count) ||
            !same_constraints(d.declaration, n))
            return not_supported(n, "temp.class");
        /* [temp.param]/12: "A template-parameter shall not be given
           default arguments by two different declarations in the same
           scope"; /10: "The set of default template-arguments available
           for use is obtained by merging the default arguments from all
           prior declarations": the definition's parameters take the
           declaration's defaults, if their names are the same (the
           default's names are the definition's when analysed) */
        for (uint32_t i = 0; i < d.parameter_count; i++) {
            SyntaxRef before = template_parameter_nodes[d.first_parameter + i];
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
    SyntaxRef n, SyntaxRef specifier, const owned_array<SyntaxRef> &parameters)
{
    const char *rule = "temp.class.spec";
    if (parameters.empty())
        rule = "temp.expl.spec";
    entity_index outer = current_scope;
    /* [temp.class.spec]/5: a partial specialization of a member class
       template is declared in the member's class (member_template_declaration
       leaves defining_class clear); [temp.expl.spec]'s are not analysed
       there ([temp.expl.spec]/2) */
    bool member = entity_table[outer].kind == entity_kind::class_type;
    if ((!is_namespace(outer) && !(member && !parameters.empty())) ||
        defining_class)
        return not_supported(n, rule);
    /* [temp.expl.spec]/1 also declares a specialization without defining
       it: class-key simple-template-id ; (an elaborated-type-specifier),
       whose definition may follow */
    bool defines = specifier.is(syntax_kind::class_specifier);
    /* the specialization's simple-template-id: a name that is a token
       (a plain identifier) is not one */
    auto written = specifier[1];
    if (defines)
        written = class_head_name(specifier[0]);
    if (!written.is_node())
        return not_supported(n, rule);
    lookup_result x;
    entity_index named = 0;
    if (!template_id_name(written, x, named))
        return not_supported(n, rule);
    uint32_t ct = class_template_of(named);
    /* [temp.class.spec]/6, [temp.expl.spec]/3: declared where the
       primary template is, by its name (not an injected-class-name) */
    if (!ct || function_templates[ct - 1].name != named ||
        entity_table[function_templates[ct - 1].entity].enclosing != outer)
        return not_supported(n, rule);
    const function_template primary = function_templates[ct - 1];
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY(r, template_parameter_scope(n, parameters, outer, 0, index, scope,
                                     first));
    /* the arguments, in the parameters' scope; what they record for
       claims is the template's */
    owned_array<template_argument> arguments;
    partial_arguments++;
    declaring_specializations++;
    r = template_id_arguments_in(written, ct, scope, arguments);
    partial_arguments--;
    declaring_specializations--;
    if (r != analysis_result::well_formed)
        return r;
    bool dependent = dependent_arguments(arguments);
    bool identity = primary_arguments(n, primary.declaration, arguments, first,
                                      parameters.size());
    /* [temp.expl.spec]: no dependent argument; [temp.class.spec]/9: "The
       specialization shall be more specialized than the primary template"
       (primary_arguments) */
    if (parameters.empty() == dependent || identity)
        return not_supported(n, rule);
    /* [temp.expl.spec]/7, [temp.class.spec]/1: one declared after a
       specialization it would be used for was instantiated makes the
       program ill-formed (no diagnostic required); any instantiation
       before it is not analysed.  One of an explicit specialization is
       that specialization's own (declared, it is defined at its next
       use), never another's.  One named but not defined yet (a
       specialization's head, instantiate_class) is not instantiated */
    /* only an instantiation whose arguments this one matches would have
       used it: [temp.class.spec]/1, [temp.class.spec.match]/2 */
    for (const instantiation_record &r : instantiations)
        if (r.template_entity == primary.entity && r.specialization &&
            entity_table[r.specialization].kind == entity_kind::class_type &&
            (entity_table[r.specialization].defined || r.defining) &&
            !(r.pattern &&
              !function_templates[r.pattern - 1].parameter_count) &&
            r.argument_count == arguments.size()) {
            owned_array<template_argument> d;
            owned_array<bool> set;
            for (size_t i = 0; i < parameters.size(); i++) {
                d.append(template_argument());
                set.append(false);
            }
            bool match = true;
            matching_partial = true;
            for (size_t i = 0; i < arguments.size() && match; i++) {
                const template_argument &y =
                    template_arguments[r.first_argument + i];
                if (arguments[i].kind == template_argument::kind_t::pack &&
                    y.kind == template_argument::kind_t::pack)
                    match = deduce_pack_argument(arguments[i], y, d, set);
                else
                    match = deduce_argument(arguments[i], y, d, set);
            }
            matching_partial = false;
            if (match)
                return not_supported(n, rule);
        }
    /* [temp.class.spec]/1: a partial specialization declared only is
       recorded by its definition */
    if (!defines && !parameters.empty())
        return analysis_result::well_formed;
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
 * partial-ordering.cpp).
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
    SyntaxRef id = 0;
    if (s.is_variable) {
        id = variable_specialization_id(s.declarator);
    } else {
        SyntaxRef head = s.definition[0].index();
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
    lookup_horizon = s.declaration.first_token();
    substituting++;
    partial_arguments++;
    declaring_specializations++;
    analysis_result r = template_id_arguments_in(id, s.primary, scope, again);
    partial_arguments--;
    declaring_specializations--;
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
               context: not analysed; nor one of a pattern (no position) */
            if (k + 1 != x.count || e.value == UINT32_MAX) {
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
    SyntaxRef at, uint32_t class_template, const template_argument *arguments,
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
        matching_partial = true;
        for (uint32_t i = 0; i < count && match; i++) {
            const template_argument x = template_arguments[s.first_pattern + i];
            const template_argument &y = arguments[i];
            if (x.kind == template_argument::kind_t::pack &&
                y.kind == template_argument::kind_t::pack)
                match = deduce_pack_argument(x, y, d, set);
            else
                match = deduce_argument(x, y, d, set);
        }
        matching_partial = false;
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
        VTRY(again, substitute_specialization(k + 1, d, arguments, count,
                                              substituted));
        if (!substituted)
            continue;
        /* [temp.class.spec.match]/2: "and the deduced template arguments
           satisfy the associated constraints of the partial
           specialization, if any" */
        bool satisfied = true;
        VTRY(c, template_constraints_satisfied(k + 1, d, satisfied));
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
analysis_result semantic_analysis::template_type(SyntaxRef id,
                                                 entity_index scope,
                                                 bool qualified,
                                                 type_index &type, uint32_t *cv)
{
    if (id.size() < 3 || !id[0].is_identifier())
        return not_supported(id, "temp.names");
    uint32_t name = tokens.identifier(id[0].token());
    lookup_result x;
    if (qualified)
        x = lookup_qualified(scope, name, lookup_filter::any);
    else
        x = lookup_unqualified(name, lookup_filter::any);
    entity_index named = x.entity;
    if (!named)
        named = x.unknown_entity;
    if (!named && qualified && !x.unknown) {
        VTRY(m, missing_member(id[0].token(), scope));
    }
    if (!named && !qualified && is_builtin_common_type(name))
        return builtin_common_type(id, type);
    if (!named && !qualified && is_builtin_type_pack_element(name))
        return builtin_type_pack_element(id, type);
    if (!named && !qualified && is_builtin_make_integer_seq(name))
        return builtin_make_integer_seq(id, type);
    uint32_t ct = class_template_of(named);
    if (!ct) {
        uint32_t at = alias_template_of(named);
        if (at)
            return alias_template_type(id, at, type, cv);
        return not_supported(id, "temp.names");
    }
    owned_array<template_argument> arguments;
    VTRY(filled, template_id_arguments(id, ct, arguments));
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
        type = dependent_specialization(function_templates[ct - 1].entity,
                                        arguments);
        return analysis_result::well_formed;
    }
    /* [temp.names]/8: "When the template-name of a simple-template-id
       names a constrained non-function template ... the constraints of
       that template shall be satisfied" ([temp.constr.decl]/3: its
       requires-clause) */
    if (head_constraint(function_templates[ct - 1].declaration)) {
        bool satisfied = true;
        VTRY(s, template_constraints_satisfied(ct, arguments, satisfied));
        if (!satisfied && substituting)
            return analysis_result::ill_formed;
        if (!satisfied)
            return ill_formed(id,
                              "the template's constraints are not satisfied",
                              "temp.names");
    }
    entity_index c = 0;
    VTRY(r, instantiate_class(id, ct, &arguments[0],
                              static_cast<uint32_t>(arguments.size()), c));
    type = entity_table[c].type.unqualified();
    return analysis_result::well_formed;
}

/*
 * [temp.names]/1: a simple-template-id's template-arguments for the class
 * template function_templates[ct - 1], in order, then [temp.arg]/4's
 * defaults for those not given.
 */
analysis_result semantic_analysis::template_id_arguments(
    SyntaxRef id, uint32_t ct, owned_array<template_argument> &arguments)
{
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
    owned_array<SyntaxElement> written;
    owned_array<bool> expansion;
    for (uint32_t i = 2; i + 1 < id.size(); i++) {
        if (id[i].is(preprocessing_op_or_punc::ELLIPSIS)) {
            if (written.empty() || expansion.back())
                return not_supported(id, "temp.variadic");
            expansion.back() = true;
            continue;
        }
        if (id[i].is(syntax_kind::template_argument_list)) {
            if (!expansion_list(id[i], written, expansion))
                return not_supported(id, "temp.variadic");
        } else {
            written.append(id[i]);
            expansion.append(false);
        }
    }
    /* a type-id that ends in the ellipsis: its pattern the type-id before
       it */
    for (size_t w = 0; w < written.size(); w++)
        if (written[w].is(syntax_kind::type_id) && written[w].size() == 2 &&
            written[w][1].is(preprocessing_op_or_punc::ELLIPSIS)) {
            if (expansion[w])
                return not_supported(id, "temp.variadic");
            expansion[w] = true;
        }
    {
        for (size_t w = 0; w < written.size(); w++) {
            SyntaxElement a = written[w];
            /* [temp.variadic]/5: a pack expansion's pattern, once per
               element of an instantiation's packs (a template's own
               pack expansion is not analysed) */
            uint32_t times = 1;
            if (expansion[w]) {
                token_index first = first_token(a);
                token_index end = first + 1;
                if (!a.is_token())
                    end = a.end_token();
                if (end > first + 1 && SyntaxElement::from_token(end - 1).is(
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
                /* a pattern naming the template's own packs (in its own
                   declaration, __enable_if_t<_Pred::value>...): one
                   element of unknown count, the pattern's dependent
                   written type, analysed again with the elements when
                   the template is instantiated */
                bool own_pattern = false;
                if (!expansion_count(first, end, times, &own_pattern)) {
                    if (!own_pattern || a.is_token() || pack >= count ||
                        arguments.size() < pack)
                        return not_supported(id, "temp.variadic");
                    template_argument v;
                    v.kind = template_argument::kind_t::pack;
                    v.type = qualified_type::make(dependent_written_type(a),
                                                  cv_none);
                    v.template_entity = type_table[v.type.unqualified()].entity;
                    v.value = UINT32_MAX;
                    v.count = UINT32_MAX;
                    elements.append(v);
                    continue;
                }
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
        /* (a non-type parameter's type: the same, or each list's type
           parameter at the same position, template<class T, T...>) */
        if (a.kind == entity_kind::none && !(a.type == b.type)) {
            uint32_t at =
                own_type_parameter_position(a.type, a.enclosing, first);
            if (at == UINT32_MAX || a.type.cv() != b.type.cv() ||
                at != own_type_parameter_position(b.type, b.enclosing,
                                                  other_first))
                return false;
        }
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
semantic_analysis::class_template_declaration(SyntaxRef n, SyntaxRef elaborated)
{
    entity_index outer = current_scope;
    if (elaborated.size() == 2 && elaborated[0].is_token() &&
        elaborated[1].is(syntax_kind::simple_template_id)) {
        /* [temp.class.spec]/1: template<class A> class S<bool, A>; a
           partial specialization declared, its definition later */
        standard_keyword k = elaborated[0].keyword();
        if (k != standard_keyword::class_ && k != standard_keyword::struct_)
            return not_supported(n, "temp.class.spec");
        owned_array<SyntaxRef> list;
        VTRY(r, template_head_parameters(n, list, true, true, true, true));
        return class_specialization_definition(n, elaborated, list);
    }
    if (elaborated.size() != 2 || !elaborated[0].is_token() ||
        !elaborated[1].is_identifier())
        return not_supported(n, "temp.class");
    standard_keyword key = elaborated[0].keyword();
    if (key != standard_keyword::class_ && key != standard_keyword::struct_ &&
        key != standard_keyword::union_)
        return not_supported(n, "temp.class");
    owned_array<SyntaxRef> parameters;
    /* [temp.pre]/9: a requires-clause in its template-head */
    VTRY(r, template_head_parameters(n, parameters, true, true, false, true));
    uint32_t name = tokens.identifier(elaborated[1].token());
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY_SET(r, template_parameter_scope(n, parameters, outer, name, index,
                                         scope, first));
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
                                      d.parameter_count) ||
            !same_constraints(d.declaration, n))
            return not_supported(n, "temp.class");
        for (SyntaxRef x : parameters)
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
SyntaxRef semantic_analysis::out_of_class_member(SyntaxRef definition,
                                                 token_index *name) const
{
    /* ([ext:gnu_attributes] before the declaration are not among its
       parts) */
    SyntaxTail parts(definition, leading_gnu_attributes(definition));
    /* (a constructor or destructor has no decl-specifier-seq) */
    size_t at = parts.size() - 2;
    if ((parts.size() != 3 && parts.size() != 2) || parts[at].is_token() ||
        (parts.size() == 3 && !parts[0].is(syntax_kind::decl_specifier_seq)))
        return 0;
    auto x = parts[at];
    while (x.is(syntax_kind::ptr_declarator)) {
        auto pd = x;
        if (pd.size() != 2 || x[1].is_token())
            return 0;
        x = x[1];
    }
    if (!x.is(syntax_kind::noptr_declarator) || !x.size() ||
        !x[0].is(syntax_kind::qualified_id))
        return 0;
    auto q = x[0];
    if (q.size() != 2 || !q[0].is(syntax_kind::nested_name_specifier))
        return 0;
    /* (an operator function's name is made when the declarator is
       analysed: [over.oper]) */
    bool operator_name = static_cast<bool>(q[1].operator_function_id());
    /* ([class.dtor]/1: ~ and the class's name) */
    bool destructor = q[1].is(syntax_kind::unqualified_id) &&
                      q[1].size() == 2 && q[1][1].is_identifier();
    if (!operator_name && !destructor && !q[1].is_identifier())
        return 0;
    auto nested = q[0];
    if (name && destructor)
        *name = q[1][1].token();
    else if (name && operator_name)
        *name = q[1].operator_function_id().operator_keyword().token();
    else if (name)
        *name = q[1].token();
    if (nested.size() != 2 || !nested[0].is(syntax_kind::simple_template_id))
        return 0;
    return nested[0].index();
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
analysis_result semantic_analysis::member_definition_of_template(
    SyntaxRef n, SyntaxRef definition, SyntaxRef inner)
{
    entity_index outer = current_scope;
    owned_array<SyntaxRef> parameters;
    VTRY(r, template_head_parameters(n, parameters));
    SyntaxTail parts(definition, leading_gnu_attributes(definition));
    /* (a constructor or destructor has no decl-specifier-seq) */
    if ((parts.size() != 3 && parts.size() != 2) ||
        !parts[parts.size() - 1].is(syntax_kind::function_body))
        return not_supported(n, "temp.mem.func");
    /* [temp.param]/12: "A default template-argument shall not be specified
       in the template-parameter-lists of the definition of a member of a
       class template that appears outside of the member's class." */
    for (SyntaxRef x : parameters)
        if (template_parameter_has_default(x))
            return not_supported(x, "temp.param");
    SyntaxRef id = out_of_class_member(definition);
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
    VTRY_SET(r, template_parameter_scope(n, parameters, outer, 0, index, scope,
                                         first));
    /* the template-id's arguments in terms of the parameters; what they
       record for claims is the template's */
    owned_array<template_argument> arguments;
    VTRY_SET(r, template_id_arguments_in(id, ct, scope, arguments));
    uint32_t pattern = 0;
    owned_array<template_argument> deduced;
    VTRY_SET(r, matching_specialization(id, ct, arguments.data(),
                                        static_cast<uint32_t>(arguments.size()),
                                        pattern, deduced));
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
    record.inner = inner;
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

/* [class.ctor]/1, [class.dtor]/1: the constructor or destructor of the
   specialization that a definition outside the class defines (they are not
   found by name); 0 for any other member */
entity_index
semantic_analysis::defined_special_member(entity_index specialization,
                                          const template_member_definition &o,
                                          token_index name)
{
    if (tokens.identifier(name) != entity_table[specialization].name)
        return 0;
    entity_index d = entity_table[specialization].destructor;
    if (d && (declared_by(d, o.definition) || declared_by(d, o.declaration)))
        return d;
    for (entity_index k = entity_table[specialization].first_constructor; k;
         k = entity_table[k].next_constructor)
        if (declared_by(k, o.definition) || declared_by(k, o.declaration))
            return k;
    return 0;
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
    if (o.inner)
        return define_member_template(member, index, recorded);
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
        defined = defined_special_member(x.specialization, o, name);
        if (!defined) {
            entity_index f =
                entity_table.find(x.specialization, tokens.identifier(name));
            if (f && entity_table[f].kind == entity_kind::function &&
                (declared_by(f, o.definition) || declared_by(f, o.declaration)))
                defined = f;
        }
        if (!defined)
            r = analysis_result::unsupported;
    }
    if (r == analysis_result::well_formed) {
        entity_table[defined].inline_ = true;
        return r;
    }
    return member_definition_unsupported(o.declaration, recorded);
}

/* a member's definition outside its class that is not known to be
   well-formed for a specialization of the class: unsupported, and the
   declaration already analysed (recorded) too */
analysis_result
semantic_analysis::member_definition_unsupported(SyntaxRef declaration,
                                                 bool recorded)
{
    size_t unsupported_before = unsupported.size();
    not_supported(declaration, "temp.inst");
    if (!recorded)
        return analysis_result::unsupported;
    for (analysed_declaration &d : declarations) {
        if (d.node != declaration || d.result != analysis_result::well_formed)
            continue;
        d.result = analysis_result::unsupported;
        d.first_unsupported = static_cast<uint32_t>(unsupported_before);
        d.has_unsupported = unsupported.size() > unsupported_before;
    }
    return analysis_result::unsupported;
}

/*
 * [temp.mem]/1, [temp.mem.func]/1: the member function template of a
 * class template, defined outside it (template<class T> template<class U>
 * R S<T>::f(P) {...}), for the specialization of the class that
 * instantiations[index] made: the definition of the member template of the
 * specialization that declares it (the declaration's template-head and
 * function type the definition's, [temp.over.link]/7, the same parameter
 * names so that the body finds the declaration's), kept for its
 * specializations (instantiate_definition).
 */
analysis_result semantic_analysis::define_member_template(uint32_t member,
                                                          uint32_t index,
                                                          bool recorded)
{
    const template_member_definition o = template_member_definitions[member];
    entity_index c = instantiations[index].specialization;
    owned_array<SyntaxRef> parameters;
    instantiation_context context;
    enter_instantiation(context, entity_table[c].enclosing, o.declaration);
    if (template_head_parameters(o.inner, parameters, true, true, true, true) !=
            analysis_result::well_formed ||
        head_constraint(o.inner) || trailing_constraint(o.inner)) {
        leave_instantiation(context);
        return member_definition_unsupported(o.declaration, recorded);
    }
    for (SyntaxRef x : parameters)
        if (template_parameter_has_default(x)) {
            leave_instantiation(context);
            return member_definition_unsupported(o.declaration, recorded);
        }
    current_scope = c;
    uint32_t number = 0, first = 0;
    entity_index scope = 0;
    specified s;
    declarator_meaning m;
    qualified_type type;
    uint32_t lead = leading_gnu_attributes(o.definition);
    analysis_result r = template_parameter_scope(o.inner, parameters, c, 0,
                                                 number, scope, first);
    /* (the parameters' scope stands for the namespace of the qualified
       declarator-id, as define_template_member's) */
    template_member_scope = scope;
    if (r == analysis_result::well_formed)
        r = function_template_declarator(o.definition, lead, c, scope, s, m,
                                         type);
    uint32_t cv = function_cv;
    leave_instantiation(context);
    entity_index held = 0;
    if (r == analysis_result::well_formed && m.has_id)
        held = entity_table.find(c, tokens.identifier(m.id));
    if (held)
        held = member_templates_of(held);
    if (!held)
        return member_definition_unsupported(o.declaration, recorded);
    /* the declaration the definition matches: not defined yet, the same
       template-parameters and function type ([temp.over.link]/7) */
    owned_array<uint32_t> group;
    function_template_overloads(held, group);
    for (uint32_t g : group) {
        const function_template f = function_templates[g - 1];
        const entity old = entity_table[f.entity];
        if (f.definition || old.enclosing != c ||
            f.parameter_count != parameters.size() ||
            !same_template_parameters(first, f.first_parameter,
                                      f.parameter_count) ||
            old.member_cv != cv || old.constexpr_ != s.constexpr_)
            continue;
        owned_array<template_argument> values;
        owned_array<bool> set;
        for (uint32_t i = 0; i < f.parameter_count; i++) {
            values.append(template_argument());
            set.append(false);
        }
        bool same = deduce_type(old.type, type, values, set, true);
        for (uint32_t i = 0; i < f.parameter_count && same; i++) {
            entity_index pe = template_parameter_entities[first + i];
            entity_index de =
                template_parameter_entities[f.first_parameter + i];
            if (entity_table[pe].name != entity_table[de].name ||
                (set[i] &&
                 (entity_table[pe].kind != entity_kind::typedef_name ||
                  !(values[i].type == entity_table[pe].type))))
                same = false;
        }
        if (!same)
            continue;
        function_template &record = function_templates[g - 1];
        record.declaration = o.declaration;
        record.definition = o.definition;
        entity_table[f.entity].defined = true;
        return analysis_result::well_formed;
    }
    return member_definition_unsupported(o.declaration, recorded);
}

/* a template's entity named name in outer, declared by n
   (function_templates[index]), and its record of parameters
   template_parameter_entities[first .. + count); [basic.link]/4: a name of
   an unnamed namespace has internal linkage */
semantic_analysis::function_template
semantic_analysis::new_template(uint32_t name, entity_index outer, SyntaxRef n,
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
uint32_t
semantic_analysis::attributes_without_effect_before(SyntaxRef declaration) const
{
    uint32_t k = 0;
    while (k < declaration.size() &&
           class_head_attributes_without_effect(declaration[k]))
        k++;
    return k;
}

/* [temp.variadic]/7: whether each of the tokens [first, end) that is the
   pack's name is within the pattern of a pack expansion: an ellipsis
   follows it that ends the template argument or parenthesized element it
   is in, or one enclosing it (A..., tuple<A>&..., x<y<A::v>...>), not a
   sibling's (res<A, B...>) */
bool semantic_analysis::pack_only_expanded(uint32_t name, token_index first,
                                           token_index end) const
{
    auto is = [&](token_index t, preprocessing_op_or_punc op) {
        return SyntaxElement::from_token(t).is(op);
    };
    for (token_index t = first; t < end; t++) {
        if (tokens[t].kind != token_kind::identifier ||
            tokens.identifier(t) != name)
            continue;
        /* depth of brackets from the name's; level the element the scan
           is in, sibling once a comma passed into the next element */
        int depth = 0, level = 0;
        bool sibling = false, expanded = false;
        for (token_index u = t + 1; u < end && !expanded; u++) {
            if (is(u, preprocessing_op_or_punc::LESS) ||
                is(u, preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
                is(u, preprocessing_op_or_punc::LEFT_BRACKET))
                depth++;
            else if (is(u, preprocessing_op_or_punc::GREATER) ||
                     is(u, preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
                     is(u, preprocessing_op_or_punc::RIGHT_BRACKET))
                depth--;
            else if (is(u, preprocessing_op_or_punc::GREATER_GREATER))
                depth -= 2;
            else if (is(u, preprocessing_op_or_punc::COMMA) && depth == level)
                sibling = true;
            else if (is(u, preprocessing_op_or_punc::ELLIPSIS) &&
                     depth == level && !sibling)
                expanded = true;
            if (depth < level) {
                level = depth;
                sibling = false;
            }
        }
        if (!expanded)
            return false;
    }
    return true;
}

} // namespace cplusplus
