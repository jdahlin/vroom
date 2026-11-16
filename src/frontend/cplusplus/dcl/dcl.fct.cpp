/*
 * dcl.fct.cpp: [dcl.fct] Functions: the parameters of a function
 * declarator.
 *
 *     parameters-and-qualifiers:
 *         ( parameter-declaration-clause ) cv-qualifier-seq_opt
 *             ref-qualifier_opt noexcept-specifier_opt attribute-specifier-seq_opt
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * Whether a '(' in an abstract declarator, or a parameter's declarator,
 * starts parameters-and-qualifiers rather than ( ptr-abstract-declarator ).
 * [dcl.ambig.res]/3: "the choice is between the declaration of a parameter
 * of type pointer to function and the declaration of a parameter with
 * redundant parentheses around the declarator-id. The resolution is to
 * consider the type-name as a simple-type-specifier rather than a
 * declarator-id."  So '(' followed by ')', '...' or anything that starts a
 * decl-specifier-seq (a type-name among them) is parameters.
 */
bool parser::parameters_follow(declarator_form form)
{
    if (!at(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return false;
    if (at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, 1) ||
        at(preprocessing_op_or_punc::ELLIPSIS, 1))
        return true;
    if (at_attribute_specifier(1))
        return true; /* a parameter's attribute-specifier-seq */
    /* a type-name, not a name the parser cannot classify: (a) with a
       undeclared is a parenthesized declarator-id (unless this is the
       reading in which such a name is a type) */
    position++;
    bool specifier = at_decl_specifier(unknown_names_are_types);
    position--;
    (void)form;
    return specifier;
}

node_index parser::parameters_and_qualifiers(scope_index *parameters)
{
    /* [temp.names]/3: a > in brackets nested in a template-argument-list
       is an operator */
    flag_set greater(greater_is_operator, true);
    size_t mark = begin_node();
    token_index first = position;
    add_token();
    /* [basic.scope.param]/1: "A function parameter (including one appearing
       in a lambda-declarator) or function-local predefined variable has
       function parameter scope. ... If the nearest enclosing function
       declarator is not the declarator of a function definition, the
       potential scope ends at the end of that function declarator": the
       noexcept-specifier is in it, and so is the trailing-return-type
       (dcl.decl.cpp enters the scope again for it) */
    struct parameter_scope {
        declared_names &names;
        ~parameter_scope()
        {
            names.leave();
        }
    } scope{names};
    scope_index made = names.enter(scope_kind::function_prototype);
    if (parameters)
        *parameters = made;
    parameter_declaration_clause();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to close the parameters", "dcl.fct");
    cv_qualifier_seq();
    /* ref-qualifier: & or && */
    if (!accept(preprocessing_op_or_punc::AMPERSAND))
        accept(preprocessing_op_or_punc::AMPERSAND_AMPERSAND);
    if (at(standard_keyword::noexcept_)) {
        noexcept_specifier();
        if (failed())
            return 0;
    } else if (at(standard_keyword::throw_)) {
        /* C++14's parameters-and-qualifiers has exception-specification_opt
           here, a dynamic-exception-specification or noexcept-specifier */
        dynamic_exception_specification();
        if (failed())
            return 0;
    }
    attribute_specifier_seq(true);
    if (failed())
        return 0;
    return finish_node(syntax_kind::parameters_and_qualifiers, mark, first);
}

/*
 *     parameter-declaration-clause:
 *         parameter-declaration-list_opt ..._opt
 *         parameter-declaration-list , ...
 *
 *     parameter-declaration-list:
 *         parameter-declaration
 *         parameter-declaration-list , parameter-declaration
 *
 * Nothing when the clause is empty.
 */
node_index parser::parameter_declaration_clause()
{
    size_t mark = begin_node();
    token_index first = position;
    if (at(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return 0;
    if (accept(preprocessing_op_or_punc::ELLIPSIS))
        return finish_node(syntax_kind::parameter_declaration_clause, mark,
                           first);
    size_t list_mark = begin_node();
    token_index list_first = position;
    unsigned count = 0;
    bool ellipsis = false;
    for (;;) {
        parameter_declaration();
        if (failed())
            return 0;
        count++;
        if (at(preprocessing_op_or_punc::COMMA) &&
            at(preprocessing_op_or_punc::ELLIPSIS, 1)) {
            if (count > 1)
                finish_node(syntax_kind::parameter_declaration_list, list_mark,
                            list_first);
            add_token();
            add_token();
            ellipsis = true;
            break;
        }
        if (!accept(preprocessing_op_or_punc::COMMA))
            break;
    }
    if (!ellipsis) {
        if (count > 1)
            finish_node(syntax_kind::parameter_declaration_list, list_mark,
                        list_first);
        /* parameter-declaration-list ...: [dcl.fct]/4 "Where syntactically
           correct and where ... is not part of an abstract-declarator, , ...
           is synonymous with ..." */
        ellipsis = accept(preprocessing_op_or_punc::ELLIPSIS);
    }
    if (!ellipsis)
        return 0; /* parameter-declaration-list alone passes through */
    return finish_node(syntax_kind::parameter_declaration_clause, mark, first);
}

/*
 *     parameter-declaration:
 *         attribute-specifier-seq_opt decl-specifier-seq declarator
 *         attribute-specifier-seq_opt decl-specifier-seq declarator = initializer-clause
 *         attribute-specifier-seq_opt decl-specifier-seq abstract-declarator_opt
 *         attribute-specifier-seq_opt decl-specifier-seq abstract-declarator_opt = initializer-clause
 *
 * [dcl.fct.default]/1: "If an initializer-clause is specified in a
 * parameter-declaration this initializer-clause is used as a default
 * argument."
 */
node_index parser::parameter_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    attribute_specifier_seq();
    if (failed())
        return 0;
    /* C++23's explicit object parameter (P0847R7, "this
       decl-specifier-seq declarator") is not C++20's grammar: a later
       edition's, P9's */
    if (at(standard_keyword::this_))
        return beyond_this_step();
    decl_specifier_facts specifiers;
    /* [temp.res]/5: where the parameter's context says (a member's,
       a requires-expression's, a template-parameter's ...) */
    specifiers.implicit_typename =
        parameters_implicit_typename &&
        tokens.files.behavior.has(language_feature::implicit_typename);
    decl_specifier_seq(specifiers, false);
    if (failed())
        return 0;
    if (!specifiers.node)
        return expected("a decl-specifier-seq", "dcl.fct");
    /* [dcl.typedef]/1: "it shall not be used in the decl-specifier-seq of
       a parameter-declaration" */
    if (specifiers.typedef_)
        return violates("no typedef in a parameter-declaration", "dcl.typedef",
                        specifiers.typedef_at);
    declarator_facts facts;
    declarator(declarator_form::either, facts);
    if (failed())
        return 0;
    if (facts.name && !facts.qualified_id)
        names.declare_in(names.current(), facts.name, name_kind::other);
    if (accept(preprocessing_op_or_punc::EQUAL)) {
        /* [temp.res]/5: not in a default argument */
        flag_set implicit(parameters_implicit_typename, false);
        if (list_initialization_missing())
            return 0;
        if (collecting_members) {
            /* a default argument is a complete-class context
               ([class.mem]/6): not read in the first pass over the
               member-specification (class.mem.cpp) */
            static const preprocessing_op_or_punc stops[] = {
                preprocessing_op_or_punc::COMMA,
                preprocessing_op_or_punc::RIGHT_PARENTHESIS,
                preprocessing_op_or_punc::ELLIPSIS};
            unparsed_until(stops, 3);
        } else if (member_declarator_context) {
            complete_class_context context(names);
            initializer_clause();
        } else {
            initializer_clause();
        }
        if (failed())
            return 0;
    }
    return finish_node(syntax_kind::parameter_declaration, mark, first);
}

} // namespace cplusplus
