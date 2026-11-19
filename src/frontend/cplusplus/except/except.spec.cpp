/*
 * except.spec.cpp: [except.spec] Exception specifications, as far as a
 * declarator needs them.
 *
 *     noexcept-specifier:
 *         noexcept ( constant-expression )
 *         noexcept
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::noexcept_specifier()
{
    /* [temp.names]/3: a > in brackets nested in a template-argument-list
       is an operator */
    flag_set greater(greater_is_operator, true);
    size_t mark = begin_node();
    token_index first = position;
    add_token();
    if (accept(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        constant_expression();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("')' to close the noexcept-specifier",
                            "except.spec");
    }
    return finish_node(syntax_kind::noexcept_specifier, mark, first);
}

/*
 * C++14's [except.spec]/1:
 *
 *     dynamic-exception-specification:
 *         throw ( type-id-list_opt )
 *
 *     type-id-list:
 *         type-id ..._opt
 *         type-id-list , type-id ..._opt
 *
 * [diff.cpp14.except]/1: C++17 removed it, keeping throw ( ) as a
 * noexcept-specifier ("Remove dynamic exception specifications", P0003R5), and
 * [diff.cpp17.except]/1: C++20 removed that too ("Remove throw()
 * exception specification").  In those editions they are clang's
 * extension, P9's; throw ( ... ) is Microsoft's in every edition.  C++03 has no pack expansion: the
 * ... after a type-id is C++11's (variadic templates, P7's).
 */
node_index parser::dynamic_exception_specification()
{
    size_t mark = begin_node();
    token_index first = position;
    const implementation_defined_behavior &b = tokens.files.behavior;
    bool empty = at(preprocessing_op_or_punc::LEFT_PARENTHESIS, 1) &&
                 at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, 2);
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS, 1) &&
        at(preprocessing_op_or_punc::ELLIPSIS, 2) &&
        at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, 3))
        return beyond_this_step();
    if (!b.has(language_feature::dynamic_exception_specifications)) {
        /* clang's extension of the later editions (an error unless
           -Wno-dynamic-exception-spec) */
        if (!empty)
            return beyond_this_step();
        if (!b.has(language_feature::empty_throw_exception_specification))
            return beyond_this_step();
        add_token(); /* throw */
        add_token(); /* ( */
        add_token(); /* ) */
        return finish_node(syntax_kind::noexcept_specifier, mark, first);
    }
    add_token(); /* throw */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after throw", "except.spec");
    if (!at(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
        size_t list_mark = begin_node();
        token_index list_first = position;
        unsigned count = 0;
        for (;;) {
            type_id();
            if (failed())
                return 0;
            if (at(preprocessing_op_or_punc::ELLIPSIS))
                return beyond_this_step(); /* a pack expansion: P7 */
            count++;
            if (!accept(preprocessing_op_or_punc::COMMA))
                break;
        }
        if (count > 1)
            finish_node(syntax_kind::type_id_list, list_mark, list_first);
    }
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to close the dynamic-exception-specification",
                        "except.spec");
    return finish_node(syntax_kind::dynamic_exception_specification, mark,
                       first);
}

} // namespace cplusplus

namespace cplusplus
{

/*
 * [except.spec]/2: "The exception specification is either defined
 * implicitly, or defined explicitly by using a noexcept-specifier as a
 * suffix of a function declarator.  In a noexcept-specifier, the
 * constant-expression, if supplied, shall be a contextually converted
 * constant expression of type bool ([expr.const]); that constant
 * expression is the exception specification of the function type in which
 * the noexcept-specifier appears.  A ( token that follows noexcept is part
 * of the noexcept-specifier and does not commence an initializer.  The
 * noexcept-specifier noexcept without a constant-expression is equivalent
 * to the noexcept-specifier noexcept(true)."  [except.spec]/3, C++17's
 * [except.spec]/2: "The noexcept-specifier throw() is equivalent to
 * noexcept(true)"; it is one here (parser::dynamic_exception_specification).
 * /5: noexcept(true) is a non-throwing exception specification, and with
 * -fno-exceptions nothing analysis generates depends on it but the type.
 */
analysis_result semantic_analysis::noexcept_specifier(NoexceptSpecifier n,
                                                      bool member,
                                                      bool &non_throwing)
{
    auto node = syntax(n);
    non_throwing = true;
    if (node.size() == 1 || node.size() == 3)
        /* noexcept, throw ( ) */
        return analysis_result::well_formed;
    if (node.size() != 4)
        return not_supported(n, "except.spec");
    if (member)
        /* [class.mem]/6: a member function's noexcept-specifier is a
           complete-class context, analysed with the class complete: not
           deferred yet */
        return not_supported(n, "except.spec");
    bool constant = false, value = false;
    analysis_result r = bool_constant_expression(n, node[2], constant, value);
    if (r != analysis_result::well_formed)
        return r;
    if (!constant)
        return ill_formed(n,
                          "in a noexcept-specifier, the constant-expression "
                          "shall be a contextually converted constant "
                          "expression of type bool",
                          "except.spec");
    non_throwing = value;
    return analysis_result::well_formed;
}

/*
 * [basic.scope.param]/1: a scope of its own, made current, where the
 * parameters of function_parameters are bound (a pack's elements as one
 * bound pack): what a declarator names after its
 * parameter-declaration-clause (a noexcept-specifier, a
 * trailing-return-type) finds them.
 */
void semantic_analysis::open_parameter_scope()
{
    entity_index scope = open_scope(0, true);
    owned_array<entity_index> pack;
    uint32_t pack_name = 0;
    for (const parameter_meaning &parameter : function_parameters) {
        uint32_t name = parameter.pack_name;
        if (!name && parameter.id)
            name = tokens.identifier(parameter.id);
        if (!name)
            continue;
        entity_index e = entity_table.make(entity_kind::variable, name, scope);
        entity_table[e].type = parameter.type;
        entity_table[e].automatic = true;
        if (parameter.pack_name) {
            pack.append(e);
            pack_name = parameter.pack_name;
        } else if (!entity_table.find(scope, name)) {
            entity_table.bind(scope, name, e);
        }
    }
    if (!pack_name && function_empty_pack)
        make_bound_pack(scope, function_empty_pack, nullptr, 0);
    if (pack_name && !entity_table.find(scope, pack_name))
        make_bound_pack(scope, pack_name, pack.data(),
                        static_cast<uint32_t>(pack.size()));
}

/*
 * [basic.scope.param]/1: "A function parameter (including one appearing in
 * a lambda-declarator) or function-local predefined variable
 * ([dcl.fct.def]) introduces a name in a function parameter scope", whose
 * potential scope ends at the end of the function declarator when it is
 * not a definition's: the noexcept-specifier n names the parameters of
 * function_parameters, analysed in a scope of their own.
 */
analysis_result semantic_analysis::parameter_scope_noexcept(node_index n,
                                                            bool member,
                                                            bool &non_throwing)
{
    auto node = syntax(n);
    /* [class.mem]/6: a member function template's noexcept-specifier,
       is analysed per specialization (taken as value-dependent), whose class is
       complete by then ([temp.inst]/4) */
    if (member && node.size() == 4 && in_member_template_declarator == 1) {
        non_throwing = false;
        dependent_exception_specifications.append(n);
        return analysis_result::well_formed;
    }
    if (member && in_member_template_declarator == 2)
        member = false;
    if (node.size() != 4 || member)
        return noexcept_specifier(syntax(n), member, non_throwing);
    entity_index outer = current_scope;
    open_parameter_scope();
    analysis_result r = analysis_result::well_formed;
    if (!instantiating && !class_instantiating && !substituting &&
        names_dependent_entity(node.first_token(), node.end_token()))
    /* [temp.dep.constexpr]: value-dependent in the template's own
           declaration; each specialization's declaration is analysed with
           the arguments, and the template's own type says potentially
           throwing */
    {
        non_throwing = false;
        dependent_exception_specifications.append(n);
    } else
        r = noexcept_specifier(syntax(n), member, non_throwing);
    current_scope = outer;
    return r;
}

/* whether the function template t's declaration has a noexcept-specifier
   left value-dependent */
bool semantic_analysis::dependent_exception_specification(entity_index t) const
{
    for (const function_template &f : function_templates) {
        if (f.entity != t || !f.declaration)
            continue;
        auto d = syntax(f.declaration);
        for (node_index x : dependent_exception_specifications)
            if (syntax(x).first_token() >= d.first_token() &&
                syntax(x).end_token() <= d.end_token())
                return true;
    }
    return false;
}

} // namespace cplusplus
