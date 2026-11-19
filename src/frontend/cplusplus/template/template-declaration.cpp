/*
 * template-declaration.cpp: [temp.pre] Templates: preamble, with
 * [temp.expl.spec]'s explicit-specialization and [temp.explicit]'s
 * explicit-instantiation, which begin with the same keyword.
 *
 *     template-declaration:
 *         template-head declaration
 *         template-head concept-definition
 *
 *     template-head:
 *         template < template-parameter-list > requires-clause_opt
 *
 *     template-parameter-list:
 *         template-parameter
 *         template-parameter-list , template-parameter
 *
 *     requires-clause:
 *         requires constraint-logical-or-expression
 *
 *     constraint-logical-or-expression:
 *         constraint-logical-and-expression
 *         constraint-logical-or-expression || constraint-logical-and-expression
 *
 *     constraint-logical-and-expression:
 *         primary-expression
 *         constraint-logical-and-expression && primary-expression
 *
 * [temp.expl.spec]/1:
 *
 *     explicit-specialization:
 *         template < > declaration
 *
 * [temp.explicit]/2:
 *
 *     explicit-instantiation:
 *         extern_opt template declaration
 *
 * Scopes: [basic.scope.temp]/1, "The declarative region of the name of
 * a template parameter of a template template-parameter is the smallest
 * template-parameter-list in which the name was introduced", and /2: "The
 * declarative region of the name of a template parameter of a template is
 * the smallest template-declaration in which the name was introduced."
 * The template's own name belongs to the scope around the template-head
 * (declared_names::declaring_scope), as a template ([temp.pre]/1), and the
 * template-parameter scope is searched after a member's class when an
 * out-of-class definition's declarator-id puts the names in the class
 * ([temp.local]/8).
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

/* RAII: a template-parameter scope searched by every lookup while the
   template-declaration that made it is parsed */
struct template_parameters_in_effect {
    declared_names &names;
    template_parameters_in_effect(declared_names &names, scope_index region)
        : names(names)
    {
        names.template_parameters_in_effect.append(region);
    }
    ~template_parameters_in_effect()
    {
        names.template_parameters_in_effect.resize(
            names.template_parameters_in_effect.size() - 1);
    }
    template_parameters_in_effect(const template_parameters_in_effect &) =
        delete;
    template_parameters_in_effect &
    operator=(const template_parameters_in_effect &) = delete;
};

} // namespace

/*
 * At template: a template-declaration (template <, then a parameter), an
 * explicit-specialization (template < >) or an explicit-instantiation
 * (template not followed by <).
 */
node_index parser::template_declaration(node_index (parser::*declaration)())
{
    if (!at(preprocessing_op_or_punc::LESS, 1))
        return explicit_instantiation(declaration);
    size_t mark = begin_node();
    token_index first = position;
    if (at(preprocessing_op_or_punc::GREATER, 2)) {
        /* [temp.expl.spec]/1: template < > declaration.  The declaration
           declares no template: its name is a template-id or names one
           already declared */
        add_token();
        add_token();
        add_token();
        scoped_value<scope_index> none(template_scope, UINT32_MAX);
        scoped_value<scope_index> single(single_declarator_scope,
                                         names.declaring_scope());
        flag_set explicit_(explicit_declaration, true);
        flag_set not_instantiation(explicit_instantiation_, false);
        (this->*declaration)();
        if (failed())
            return 0;
        return finish_node(syntax_kind::explicit_specialization, mark, first);
    }
    declarative_region parameters(names, scope_kind::template_parameter);
    template_parameters_in_effect in_effect(names, names.current());
    template_head();
    if (failed())
        return 0;
    /* [temp.pre]/2: the declaration of a template-declaration declares
       something: a static_assert-declaration does not (clang: "a
       static_assert declaration cannot be a template") */
    if (at(standard_keyword::static_assert_))
        return violates("a declaration that declares a name in a "
                        "template-declaration",
                        "temp.pre", position);
    scoped_value<scope_index> scope(template_scope, names.declaring_scope());
    scoped_value<scope_index> single(single_declarator_scope,
                                     names.declaring_scope());
    flag_set not_explicit(explicit_declaration, false);
    flag_set not_instantiation(explicit_instantiation_, false);
    if (at(standard_keyword::concept_))
        concept_definition();
    else
        (this->*declaration)();
    if (failed())
        return 0;
    return finish_node(syntax_kind::template_declaration, mark, first);
}

/*
 * [temp.explicit]/2: extern_opt template declaration.  "An explicit
 * instantiation definition" and "an explicit instantiation declaration"
 * (with extern, N1987, C++11; before it [ext.gnu], P9's).  The declaration
 * names a specialization that is declared already: it declares nothing
 * new (declared_names keeps a template's kind, declare_in).
 */
node_index parser::explicit_instantiation(node_index (parser::*declaration)())
{
    size_t mark = begin_node();
    token_index first = position;
    if (at(standard_keyword::extern_)) {
        if (!tokens.files.behavior.has(language_feature::extern_templates))
            return beyond_this_step();
        add_token();
    }
    add_token(); /* template */
    scoped_value<scope_index> none(template_scope, UINT32_MAX);
    scoped_value<scope_index> single(single_declarator_scope,
                                     names.declaring_scope());
    flag_set explicit_(explicit_declaration, true);
    flag_set instantiation(explicit_instantiation_, true);
    /* [temp.explicit]/3: the declaration is no explicit-instantiation, an
       explicit-specialization or a template-declaration of its own */
    if (at(standard_keyword::template_))
        return expected("a declaration after template", "temp.explicit");
    /* [dcl.attr.grammar]/5 as clang reads it: no attribute-specifier-seq
       begins an explicit instantiation's declaration */
    if (at_attribute_specifier())
        return violates("no attribute-specifier-seq in an explicit "
                        "instantiation",
                        "temp.explicit", position);
    (this->*declaration)();
    if (failed())
        return 0;
    return finish_node(syntax_kind::explicit_instantiation, mark, first);
}

bool parser::template_declarator_violates(
    const decl_specifier_facts &specifiers, bool first)
{
    if (names.declaring_scope() != single_declarator_scope)
        return false;
    if (!first) {
        violates("at most one declarator in a template-declaration, "
                 "explicit-specialization or explicit-instantiation",
                 "temp.pre", position);
        return true;
    }
    /* "When such a declaration is used to declare a class template, no
       declarator is permitted." */
    if (template_scope == names.declaring_scope() && specifiers.declares_type &&
        !specifiers.elaborated) {
        violates("no declarator after a class template's definition",
                 "temp.pre", position);
        return true;
    }
    return false;
}

node_index parser::template_head()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* template */
    if (!accept(preprocessing_op_or_punc::LESS))
        return expected("'<' after template", "temp.pre");
    {
        /* [temp.param]/16: "When parsing a default template-argument for a
           non-type template-parameter, the first non-nested > is taken as
           the end of the template-parameter-list rather than a greater-than
           operator." */
        flag_set greater(greater_is_operator, false);
        /* [class.mem]/6: a default template-argument is no complete-class
           context, so a member template's is read in the first pass over
           the member-specification too (member-declaration.cpp) */
        flag_set collecting(collecting_members, false);
        if (at_closing_angle())
            return expected("a template-parameter", "temp.pre");
        template_parameter_list();
        if (failed())
            return 0;
    }
    if (!accept_closing_angle())
        return expected("'>' to end the template-parameter-list", "temp.pre");
    if (at(standard_keyword::requires_)) {
        requires_clause();
        if (failed())
            return 0;
    }
    return finish_node(syntax_kind::template_head, mark, first);
}

node_index parser::template_parameter_list()
{
    size_t mark = begin_node();
    token_index first = position;
    unsigned count = 0;
    for (;;) {
        template_parameter();
        if (failed())
            return 0;
        count++;
        if (!accept(preprocessing_op_or_punc::COMMA))
            break;
    }
    if (count == 1)
        return 0;
    return finish_node(syntax_kind::template_parameter_list, mark, first);
}

/*
 * [temp.pre]/9: "A requires-clause ... introduces a constraint-expression
 * that results from interpreting the constraint-logical-or-expression as a
 * constraint-expression."  Its operands are primary-expressions, so
 * requires f(x) and requires !C<T> are not this grammar ([temp.pre]/... a
 * parenthesized expression is).  P0734R0 (C++20).
 */
node_index parser::requires_clause()
{
    if (!tokens.files.behavior.has(language_feature::concepts))
        return beyond_this_step();
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* requires */
    constraint_logical_or_expression();
    if (failed())
        return 0;
    return finish_node(syntax_kind::requires_clause, mark, first);
}

node_index parser::constraint_logical_or_expression()
{
    flag_set greater(greater_is_operator, true);
    /* clang's reading of [temp.pre]/9: a primary-expression followed by
       what would continue an expression (a binary operator other than &&
       and ||, a postfix operator, or a type's braced-init-list) needs
       parentheses */
    auto primary = [&]() -> node_index {
        token_index at_first = position;
        unsigned length = 0;
        /* a dependent qualified name without typename names a value
           ([temp.res]/5), so T::v { is no braced-init-list of a type */
        bool type = at_identifier() &&
                    classify_name(0, &length, false) == name_class::type;
        node_index n = primary_expression();
        if (failed())
            return 0;
        if ((binary_operator_follows() &&
             !at(preprocessing_op_or_punc::AMPERSAND_AMPERSAND) &&
             !at(preprocessing_op_or_punc::BAR_BAR)) ||
            (at(preprocessing_op_or_punc::LEFT_BRACKET) &&
             !at_attribute_specifier()) ||
            at(preprocessing_op_or_punc::DOT) ||
            at(preprocessing_op_or_punc::ARROW) ||
            at(preprocessing_op_or_punc::PLUS_PLUS) ||
            at(preprocessing_op_or_punc::MINUS_MINUS) ||
            (type && position == at_first + length &&
             at(preprocessing_op_or_punc::LEFT_BRACE)))
            return violates("parentheses around an expression that is no "
                            "primary-expression in a requires-clause",
                            "temp.pre", at_first);
        return n;
    };
    auto and_expression = [&]() -> node_index {
        size_t mark = begin_node();
        token_index first = position;
        node_index n = primary();
        if (failed())
            return 0;
        while (accept(preprocessing_op_or_punc::AMPERSAND_AMPERSAND)) {
            primary();
            if (failed())
                return 0;
            n = finish_node(syntax_kind::constraint_logical_and_expression,
                            mark, first);
        }
        return n;
    };
    size_t mark = begin_node();
    token_index first = position;
    node_index n = and_expression();
    if (failed())
        return 0;
    while (accept(preprocessing_op_or_punc::BAR_BAR)) {
        and_expression();
        if (failed())
            return 0;
        n = finish_node(syntax_kind::constraint_logical_or_expression, mark,
                        first);
    }
    return n;
}

} // namespace cplusplus
