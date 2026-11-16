/*
 * expr.prim.fold.cpp: [expr.prim.fold] Fold expressions, and
 * [expr.prim.req] Requires expressions with its subclauses.
 *
 *     fold-expression:
 *         ( cast-expression fold-operator ... )
 *         ( ... fold-operator cast-expression )
 *         ( cast-expression fold-operator ... fold-operator cast-expression )
 *
 *     fold-operator: one of
 *         +   -   *   /   %   ^   &   |   <<   >>
 *         +=  -=  *=  /=  %=  ^=  &=  |=  <<=  >>=  =
 *         ==  !=  <   >   <=  >=  &&  ||  ,    .*   ->*
 *
 * [expr.prim.fold]/3: "In a binary fold, op1 and op2 shall be the same
 * fold-operator, and either e1 shall contain an unexpanded pack or e2 shall
 * contain an unexpanded pack, but not both."  The first half is a rule on
 * the form, checked; the packs are semantic.  N4295 (C++17).
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

bool is_fold_operator(const token &t)
{
    if (t.kind != token_kind::operator_or_punctuator)
        return false;
    switch (preprocessing_op_or_punc_primary(t.op_or_punc)) {
    case preprocessing_op_or_punc::PLUS:
    case preprocessing_op_or_punc::MINUS:
    case preprocessing_op_or_punc::STAR:
    case preprocessing_op_or_punc::SLASH:
    case preprocessing_op_or_punc::PERCENT:
    case preprocessing_op_or_punc::CARET:
    case preprocessing_op_or_punc::AMPERSAND:
    case preprocessing_op_or_punc::BAR:
    case preprocessing_op_or_punc::LESS_LESS:
    case preprocessing_op_or_punc::GREATER_GREATER:
    case preprocessing_op_or_punc::PLUS_EQUAL:
    case preprocessing_op_or_punc::MINUS_EQUAL:
    case preprocessing_op_or_punc::STAR_EQUAL:
    case preprocessing_op_or_punc::SLASH_EQUAL:
    case preprocessing_op_or_punc::PERCENT_EQUAL:
    case preprocessing_op_or_punc::CARET_EQUAL:
    case preprocessing_op_or_punc::AMPERSAND_EQUAL:
    case preprocessing_op_or_punc::BAR_EQUAL:
    case preprocessing_op_or_punc::LESS_LESS_EQUAL:
    case preprocessing_op_or_punc::GREATER_GREATER_EQUAL:
    case preprocessing_op_or_punc::EQUAL:
    case preprocessing_op_or_punc::EQUAL_EQUAL:
    case preprocessing_op_or_punc::EXCLAMATION_EQUAL:
    case preprocessing_op_or_punc::LESS:
    case preprocessing_op_or_punc::GREATER:
    case preprocessing_op_or_punc::LESS_EQUAL:
    case preprocessing_op_or_punc::GREATER_EQUAL:
    case preprocessing_op_or_punc::AMPERSAND_AMPERSAND:
    case preprocessing_op_or_punc::BAR_BAR:
    case preprocessing_op_or_punc::COMMA:
    case preprocessing_op_or_punc::DOT_STAR:
    case preprocessing_op_or_punc::ARROW_STAR:
        return true;
    default:
        return false;
    }
}

} // namespace

/* at '(': whether a fold-operator followed by ... is in the parentheses
   outside the brackets nested in them, or ... begins them */
bool parser::fold_expression_follows()
{
    if (at(preprocessing_op_or_punc::ELLIPSIS, 1))
        return true;
    for (unsigned i = 1;; i++) {
        if (tokens.at_end(position + i))
            return false;
        const token &t = peek(i);
        if (is_operator(t, preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
            is_operator(t, preprocessing_op_or_punc::RIGHT_BRACKET) ||
            is_operator(t, preprocessing_op_or_punc::RIGHT_BRACE) ||
            is_operator(t, preprocessing_op_or_punc::SEMICOLON))
            return false;
        if (is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
            is_operator(t, preprocessing_op_or_punc::LEFT_BRACKET) ||
            is_operator(t, preprocessing_op_or_punc::LEFT_BRACE)) {
            unsigned end = group_end(i);
            if (end <= i + 1)
                return false;
            i = end - 1;
            continue;
        }
        if (is_fold_operator(t) && at(preprocessing_op_or_punc::ELLIPSIS, i + 1))
            return true;
    }
}

node_index parser::fold_expression()
{
    if (!tokens.files.behavior.has(language_feature::fold_expressions))
        return beyond_this_step();
    size_t mark = begin_node();
    token_index first = position;
    flag_set greater(greater_is_operator, true);
    add_token(); /* ( */
    if (at(preprocessing_op_or_punc::ELLIPSIS)) {
        /* ( ... fold-operator cast-expression ) */
        add_token();
        if (!is_fold_operator(peek()))
            return expected("a fold-operator after '...'", "expr.prim.fold");
        add_token();
        cast_expression();
        if (failed())
            return 0;
    } else {
        cast_expression();
        if (failed())
            return 0;
        if (!is_fold_operator(peek()) ||
            !at(preprocessing_op_or_punc::ELLIPSIS, 1))
            return expected("a fold-operator and '...' after the "
                            "cast-expression",
                            "expr.prim.fold");
        token_index op1 = position;
        add_token();
        add_token(); /* ... */
        if (is_fold_operator(peek())) {
            /* [expr.prim.fold]/3: "op1 and op2 shall be the same
               fold-operator" */
            if (preprocessing_op_or_punc_primary(peek().op_or_punc) !=
                preprocessing_op_or_punc_primary(tokens[op1].op_or_punc))
                return violates("the same fold-operator on both sides",
                                "expr.prim.fold", position);
            add_token();
            cast_expression();
            if (failed())
                return 0;
        }
    }
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to end the fold-expression", "expr.prim.fold");
    return finish_node(syntax_kind::fold_expression, mark, first);
}

/*
 * [expr.prim.req]:
 *
 *     requires-expression:
 *         requires requirement-parameter-list_opt requirement-body
 *
 *     requirement-parameter-list:
 *         ( parameter-declaration-clause_opt )
 *
 *     requirement-body:
 *         { requirement-seq }
 *
 *     requirement-seq:
 *         requirement
 *         requirement-seq requirement
 *
 * [expr.prim.req]/5: "The requirement-parameter-list ... introduces local
 * parameters" whose scope is the requirement-body ([basic.scope.param]);
 * /5 "A local parameter shall not have a default argument" is semantic.
 * P0734R0 (C++20).
 */
node_index parser::requires_expression()
{
    if (!tokens.files.behavior.has(language_feature::concepts))
        return beyond_this_step();
    size_t mark = begin_node();
    token_index first = position;
    flag_set greater(greater_is_operator, true);
    add_token(); /* requires */
    declarative_region parameters(names, scope_kind::function_prototype);
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        size_t list_mark = begin_node();
        token_index list_first = position;
        add_token();
        {
            /* [temp.res]/5: a parameter-declaration in a
               requirement-parameter-list */
            flag_set implicit(parameters_implicit_typename, true);
            parameter_declaration_clause();
        }
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("')' to close the requirement-parameter-list",
                            "expr.prim.req");
        finish_node(syntax_kind::requirement_parameter_list, list_mark,
                    list_first);
    }
    size_t body_mark = begin_node();
    token_index body_first = position;
    if (!accept(preprocessing_op_or_punc::LEFT_BRACE))
        return expected("'{' to begin the requirement-body", "expr.prim.req");
    size_t seq_mark = begin_node();
    token_index seq_first = position;
    unsigned count = 0;
    while (!at(preprocessing_op_or_punc::RIGHT_BRACE)) {
        if (at_end())
            return expected("'}' to end the requirement-body", "expr.prim.req");
        requirement();
        if (failed())
            return 0;
        count++;
    }
    if (!count)
        return expected("a requirement", "expr.prim.req");
    if (count > 1)
        finish_node(syntax_kind::requirement_seq, seq_mark, seq_first);
    add_token(); /* } */
    finish_node(syntax_kind::requirement_body, body_mark, body_first);
    return finish_node(syntax_kind::requires_expression, mark, first);
}

/*
 *     requirement:
 *         simple-requirement
 *         type-requirement
 *         compound-requirement
 *         nested-requirement
 *
 *     simple-requirement:
 *         expression ;
 *
 *     type-requirement:
 *         typename nested-name-specifier_opt type-name ;
 *
 *     compound-requirement:
 *         { expression } noexcept_opt return-type-requirement_opt ;
 *
 *     return-type-requirement:
 *         -> type-constraint
 *
 *     nested-requirement:
 *         requires constraint-expression ;
 *
 * Which one by the first token: requires begins a nested-requirement, {
 * a compound-requirement, typename a type-requirement when ; follows the
 * name (else it is a simple-requirement whose expression begins with a
 * typename-specifier, typename T::type{}).
 */
node_index parser::requirement()
{
    size_t mark = begin_node();
    token_index first = position;
    if (at(standard_keyword::requires_)) {
        add_token();
        binary_expression(0);
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::SEMICOLON))
            return expected("';' after the nested-requirement",
                            "expr.prim.req.nested");
        return finish_node(syntax_kind::nested_requirement, mark, first);
    }
    if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
        add_token();
        expression();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_BRACE))
            return expected("'}' after the expression",
                            "expr.prim.req.compound");
        accept(standard_keyword::noexcept_);
        if (at(preprocessing_op_or_punc::ARROW)) {
            size_t return_mark = begin_node();
            token_index return_first = position;
            add_token();
            unsigned length = 0;
            if (!type_constraint_follows(0, &length))
                return expected("a type-constraint after '->'",
                                "expr.prim.req.compound");
            type_constraint();
            if (failed())
                return 0;
            finish_node(syntax_kind::return_type_requirement, return_mark,
                        return_first);
        }
        if (!accept(preprocessing_op_or_punc::SEMICOLON))
            return expected("';' after the compound-requirement",
                            "expr.prim.req.compound");
        return finish_node(syntax_kind::compound_requirement, mark, first);
    }
    if (at(standard_keyword::typename_)) {
        tentative_parse attempt(*this);
        add_token();
        scope_index scope = 0;
        bool known = false;
        nested_name_specifier(&scope, &known);
        if (!failed() && at(standard_keyword::template_))
            add_token();
        if (!failed() && at_identifier()) {
            size_t name_mark = begin_node();
            token_index name_first = position;
            uint32_t name = tokens.identifier(position);
            add_token();
            name_kind kind = name_kind::not_declared;
            if (!known)
                kind = name_kind::template_name;
            else if (scope == names.current())
                kind = names.lookup_unqualified(name);
            else
                kind = names.lookup_qualified(scope, name);
            if (at(preprocessing_op_or_punc::LESS) && is_template_kind(kind))
                template_id_rest(name_mark, name_first,
                                 syntax_kind::simple_template_id);
            if (!failed() && accept(preprocessing_op_or_punc::SEMICOLON)) {
                attempt.commit();
                return finish_node(syntax_kind::type_requirement, mark, first);
            }
        }
    }
    expression();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the simple-requirement",
                        "expr.prim.req.simple");
    return finish_node(syntax_kind::simple_requirement, mark, first);
}

} // namespace cplusplus
