/*
 * function-template.cpp: [temp.fct] Function templates: a function template's
 * declaration and definition, and a template-id naming one's specialization by
 * its value ([temp.names], [over.over]).
 * Split from declared-templates.cpp, which has [temp.pre]'s declarations of
 * templates and what the kinds share.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"
#include "declared-templates.h"

namespace cplusplus
{

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
    else if (specifiers) {
        /* [class.conv.fct]/2: a conversion function may be explicit */
        SyntaxRef conversion;
        if (member_of)
            conversion = conversion_function_id_of(definition);
        bool outer_explicit = explicit_allowed;
        explicit_allowed = outer_explicit || conversion;
        r = decl_specifier_seq(definition[lead], s, false);
        explicit_allowed = outer_explicit;
    }
    for (uint32_t i = 0; i < lead && r == analysis_result::well_formed; i++)
        r = gnu_attribute_specifier(definition[i], s);
    if (r == analysis_result::well_formed) {
        type = s.type;
        function_cv = 0;
        function_parameters.clear();
        /* [class.conv.fct]/1: a conversion function template's type is
           its conversion-type-id's, in the template-parameters' scope */
        SyntaxRef conversion;
        if (member_of)
            conversion = conversion_function_id_of(definition);
        if (conversion && s.has_type)
            r = ill_formed(definition[lead],
                           "no return type can be specified for a "
                           "conversion function",
                           "class.conv.fct");
        else if (conversion)
            r = qualified_conversion_type(definition, conversion, type);
    }
    if (r == analysis_result::well_formed) {
        if (member_of)
            member_template_declarator(1);
        if (constructor)
            r = constructor_template_declarator(
                definition, definition[lead + specifiers], type, m);
        else
            r = declarator(definition, definition[lead + specifiers], type, m);
        if (member_of)
            member_template_declarator(0);
        if (r == analysis_result::well_formed)
            r = misplaced_function_qualifiers(definition, type, member_of != 0);
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
        (!defines &&
         (!tail[2].is(preprocessing_op_or_punc::SEMICOLON) ||
          (!tail[1].is(syntax_kind::noptr_declarator,
                       syntax_kind::ptr_declarator, syntax_kind::declarator)))))
        return not_supported(n, "temp.fct");
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    entity_index lookup_outer = outer;
    if (defining_class)
        lookup_outer = defining_class;
    VTRY(declared, template_parameter_scope(n, parameters, lookup_outer, 0,
                                            index, scope, first));
    specified s;
    declarator_meaning m;
    qualified_type type;
    VTRY(r,
         function_template_declarator(definition, lead, 0, scope, s, m, type));
    /* an operator function template (libc++'s byte& operator<<=(byte&,
       _Integer)) or a numeric literal operator template
       (template<char...> operator""_x(), [over.literal]/5) */
    if (!m.has_id || m.qualifier || m.qualified_namespace ||
        m.allocation != allocation_function::none || function_cv ||
        type_table[type.unqualified()].kind != type_kind::function)
        return not_supported(n, "temp.fct");
    if (m.literal_operator) {
        if (parameters.size() != 1 || !template_parameter_is_pack(first) ||
            entity_table[template_parameter_entities[first]].kind !=
                entity_kind::none ||
            fundamental_of(
                entity_table[template_parameter_entities[first]].type) !=
                fundamental_type::char_ ||
            type_table[type.unqualified()].parameter_count != 0)
            return not_supported(n, "over.literal");
    }
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
        if (pack &&
            made_of_template_parameter(type_table, ft.operand, &entity_table,
                                       last) &&
            !pack_only_expanded(pack_name, tail[0].first_token(),
                                tail[0].end_token()))
            return not_supported(n, "temp.variadic");
        for (uint32_t i = 0; i < ft.parameter_count; i++) {
            const struct type pt = type_table[list[i].unqualified()];
            if (pt.kind != type_kind::pack_expansion) {
                if (pack &&
                    made_of_template_parameter(type_table, list[i],
                                               &entity_table, last) &&
                    !pack_only_expanded(pack_name, tail[1].first_token(),
                                        tail[1].end_token()))
                    return not_supported(n, "temp.variadic");
                continue;
            }
            if (!pack || ft.variadic ||
                !made_of_template_parameter(type_table, pt.operand,
                                            &entity_table, last))
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
        /* [temp.friend]/9: a friend template constrained by an enclosing
           template's parameter redeclares none: it is another template of
           the name, an overload ([temp.over]) */
        if (distinct_friend_template)
            break;
        /* [temp.over.link]/7: "Two function templates are equivalent if
           they are declared in the same scope, have the same name, have
           equivalent template-heads, and have return types, parameter
           lists, and trailing requires-clauses (if any) that are
           equivalent using the rules described above to compare
           expressions involving template parameters"; one that is not is
           another template of the name, an overload ([temp.over]) */
        const function_template f = function_templates[g - 1];
        bool same = equivalent_template(
            f, n, first, static_cast<uint32_t>(parameters.size()), type);
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
        /* (equivalent_template: the same template-head and function
           type) */
        if (old.enclosing != outer || old.constexpr_ != s.constexpr_ ||
            (s.static_ && old.linkage_of_name != linkage::internal))
            return not_supported(n, "temp.over.link");
        /* [except.spec]/4 */
        VTRY(compatible,
             compatible_exceptions(
                 m.id, f.entity,
                 resolved_exceptions(m.exceptions, type, gnu_flags)));
        /* different names: the definition's are the ones its body uses
           (instantiate_definition binds the template's own), so a
           definition takes the record's parameters; a declaration leaves
           them ([temp.over.link]/6: names do not matter) */
        bool renamed = false;
        for (uint32_t i = 0; i < f.parameter_count; i++)
            if (entity_table[template_parameter_entities[first + i]].name !=
                entity_table[template_parameter_entities[f.first_parameter + i]]
                    .name)
                renamed = true;
        /* [dcl.inline]/5: an inline declaration after the definition is
           ill-formed; one before it makes the template inline */
        if (s.inline_ && !old.inline_ && f.definition)
            return not_supported(n, "dcl.inline");
        if (s.inline_)
            entity_table[f.entity].inline_ = true;
        entity_table.add_declaration(f.entity, n);
        last_function_template = earlier;
        if (!s.friend_) {
            function_templates[earlier - 1].hidden_friend = false;
            function_templates[earlier - 1].friend_declaration = false;
        }
        if (defines) {
            if (renamed) {
                VTRY(merged,
                     merge_template_defaults(n, f.first_parameter, first,
                                             f.parameter_count, false));
            }
            function_template &record = function_templates[earlier - 1];
            record.declaration = n;
            record.definition = definition;
            record.declarator = definition;
            if (renamed) {
                record.first_parameter = first;
                entity_table[f.entity].type = type;
            }
            entity_table[f.entity].defined = true;
        }
        return analysis_result::well_formed;
    }
    function_template record = new_template(
        name, outer, n, index, first, static_cast<uint32_t>(parameters.size()));
    record.gnu_flags = gnu_flags;
    record.hidden_friend = s.friend_ && defines;
    record.abi_tag = abi_tag;
    record.default_arguments = defaults;
    entity_index t = record.entity;
    entity_table[t].type = type;
    entity_table[t].defined = defines;
    entity_table[t].exceptions =
        resolved_exceptions(m.exceptions, type, gnu_flags);
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
    /* (a nominated namespace's own directives lead on: an inline namespace
       within an inline namespace is no reason by itself, only a namespace
       reached that declares the name or has names not known) */
    if (own && entity_table[outer].has_using_directive) {
        owned_array<entity_index> reached;
        reached.append(outer);
        for (size_t i = 0; own && i < reached.size(); i++) {
            if (!entity_table[reached[i]].has_using_directive)
                continue;
            for (const using_directive_record &u : using_directives) {
                if (u.scope != reached[i])
                    continue;
                bool seen = false;
                for (entity_index r2 : reached)
                    if (r2 == u.nominated)
                        seen = true;
                if (seen)
                    continue;
                reached.append(u.nominated);
                if (entity_table[u.nominated].unknown_names ||
                    entity_table.find(u.nominated, name))
                    own = false;
            }
        }
    }
    if (prior && group.empty() && own &&
        entity_table[prior].kind == entity_kind::function &&
        entity_table[prior].enclosing == outer &&
        !entity_table[prior].nonstatic_member &&
        !entity_table.bound_by_using(outer, name)) {
        overload_set(prior, functions);
        prior = make_held_name(name, outer);
        entity_table.bind(outer, name, prior);
    }
    size_t before = entity_table.size();
    /* (another function template of the name, group, keeps it: the
       name's templates are overloaded, [temp.over]) */
    /* (an operator function template is followed: operator expressions
       find it, over.match.oper) */
    entity_index held = entity_table.find(outer, name);
    if (!held) {
        held = make_held_name(name, outer);
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
    last_function_template = static_cast<uint32_t>(function_templates.size());
    return analysis_result::well_formed;
}

/*
 * [temp.names]/1 with [over.over]/2: a template-id naming one function
 * template whose template arguments are all explicit, not called: the one
 * specialization ([temp.arg.explicit]/4: "if all of the template arguments
 * can be deduced or obtained from default template-arguments, they may all
 * be omitted"; here none is), a function the expression names.  Several
 * templates of the name, or arguments left to deduce from a target type,
 * are [over.over]'s.
 */
analysis_result
semantic_analysis::function_template_id_value(SyntaxRef n, uint32_t ft,
                                              expression_meaning &m)
{
    token_index name = 0;
    uint32_t found = 0;
    bool qualified = false;
    owned_array<template_argument> specified;
    VTRY(parts, template_callee_parts(n, name, found, specified, qualified));
    if (name && name == overload_target_token) {
        /* [over.over]: the specialization template_id_for_target chose */
        overloaded_written.clear();
        overloaded_written_expansion.clear();
        overloaded_written_at = 0;
        size_t uses_before = name_uses.size();
        analysis_result named = named_entity(name, overload_target_choice, m);
        name_uses.resize(uses_before);
        return named;
    }
    if (found != ft || overloaded_written_at) {
        /* (the written arguments template_callee_parts kept for a call's
           template_call: no call follows) */
        overloaded_written.clear();
        overloaded_written_expansion.clear();
        overloaded_written_at = 0;
        return not_supported(n, "over.over");
    }
    if (specified.size() != function_templates[ft - 1].parameter_count)
        return not_supported(n, "over.over");
    for (const template_argument &a : specified)
        if (made_of_template_parameter(type_table, a.type) ||
            a.template_entity || a.kind == template_argument::kind_t::pack)
            return not_supported(n, "temp.dep");
    entity_index e = 0;
    VTRY(r, instantiate(n, function_templates[ft - 1].entity, specified.data(),
                        static_cast<uint32_t>(specified.size()), e));
    size_t uses_before = name_uses.size();
    analysis_result named = named_entity(name, e, m);
    name_uses.resize(uses_before);
    return named;
}

} // namespace cplusplus
