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
       undeclared is a parenthesized declarator-id */
    position++;
    bool specifier = at_decl_specifier(false);
    position--;
    (void)form;
    return specifier;
}

node_index parser::parameters_and_qualifiers()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token();
    /* [basic.scope.param]/1: "A function parameter (including one appearing
       in a lambda-declarator) or function-local predefined variable has
       function parameter scope." */
    names.enter(scope_kind::function_prototype);
    parameter_declaration_clause();
    names.leave();
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
        /* a dynamic-exception-specification (removed in C++20, [diff.cpp17.except]): P6 */
        return beyond_this_step();
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
 * The initializer-clause (a default argument, [dcl.fct.default]) is P3's.
 */
node_index parser::parameter_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    attribute_specifier_seq();
    if (failed())
        return 0;
    decl_specifier_facts specifiers;
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
        names.declare(facts.name, name_kind::other);
    if (accept(preprocessing_op_or_punc::EQUAL)) {
        static constexpr preprocessing_op_or_punc stops[] = {
            preprocessing_op_or_punc::COMMA};
        if (!unparsed_until(stops, 1))
            return expected("a default argument after '='", "dcl.fct.default");
    }
    return finish_node(syntax_kind::parameter_declaration, mark, first);
}

} // namespace cplusplus
