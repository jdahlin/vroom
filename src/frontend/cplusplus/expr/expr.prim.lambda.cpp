/*
 * expr.prim.lambda.cpp: [expr.prim.lambda] Lambda expressions, with
 * [expr.prim.lambda.capture].
 *
 *     lambda-expression:
 *         lambda-introducer lambda-declarator_opt compound-statement
 *         lambda-introducer < template-parameter-list > requires-clause_opt lambda-declarator_opt compound-statement
 *
 *     lambda-introducer:
 *         [ lambda-capture_opt ]
 *
 *     lambda-declarator:
 *         ( parameter-declaration-clause ) decl-specifier-seq_opt
 *             noexcept-specifier_opt attribute-specifier-seq_opt trailing-return-type_opt requires-clause_opt
 *
 *     lambda-capture:
 *         capture-default
 *         capture-list
 *         capture-default , capture-list
 *
 *     capture-default:
 *         &
 *         =
 *
 *     capture-list:
 *         capture
 *         capture-list , capture
 *
 *     capture:
 *         simple-capture
 *         init-capture
 *
 *     simple-capture:
 *         identifier ..._opt
 *         & identifier ..._opt
 *         this
 *         * this
 *
 *     init-capture:
 *         ..._opt identifier initializer
 *         & ..._opt identifier initializer
 *
 * The template-parameter-list, the requires-clause and the packs (...) are
 * P7's.  Lambdas are C++11's (N2927), init-captures C++14's (N3648),
 * * this C++17's (P0018R3) and constexpr in the lambda-declarator C++17's
 * (P0170R1); in an earlier edition each is clang's extension, P9's, as is
 * C++23's lambda-declarator without its parameters (P1102R2: [] mutable
 * { }).
 *
 * Scopes: [basic.scope.param]/1, "A function parameter (including one
 * appearing in a lambda-declarator) ... has function parameter scope", so
 * the parameters are seen in the compound-statement as a function's; and
 * [expr.prim.lambda.capture]/6: an init-capture "behaves as if it declares
 * and explicitly captures a variable of the form "auto init-capture ;"
 * whose declarative region is the lambda-expression's compound-statement",
 * so the init-captures are declared in a scope around the parameters and
 * the compound-statement.  An init-capture's initializer is the enclosing
 * context's ([expr.prim.lambda.capture]/6: "the initializer ... is
 * evaluated in the context where the lambda-expression appears"), so the
 * name is declared after it.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::lambda_expression()
{
    /* [temp.names]/3: a > in brackets nested in a template-argument-list
       is an operator */
    flag_set greater(greater_is_operator, true);
    const implementation_defined_behavior &b = tokens.files.behavior;
    if (!b.has(language_feature::lambda_expressions))
        return beyond_this_step();
    size_t mark = begin_node();
    token_index first = position;
    declarative_region captures(names, scope_kind::block);
    lambda_introducer();
    if (failed())
        return 0;
    /* lambda-introducer < template-parameter-list > requires-clause_opt
       lambda-declarator compound-statement (P0428R2, C++20) */
    declarative_region template_parameters(names,
                                           scope_kind::template_parameter);
    if (at(preprocessing_op_or_punc::LESS)) {
        if (!b.has(language_feature::lambda_template_parameters))
            return beyond_this_step();
        add_token();
        {
            flag_set closing(greater_is_operator, false);
            if (at_closing_angle())
                return expected("a template-parameter", "expr.prim.lambda");
            template_parameter_list();
            if (failed())
                return 0;
        }
        if (!accept_closing_angle())
            return expected("'>' to end the template-parameter-list",
                            "expr.prim.lambda");
        if (at(standard_keyword::requires_)) {
            requires_clause();
            if (failed())
                return 0;
        }
    }
    scope_index enclosing = names.current();
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        scope_index parameters = 0;
        lambda_declarator(&parameters);
        if (failed())
            return 0;
        names.reenter(parameters);
    } else if (!at(preprocessing_op_or_punc::LEFT_BRACE)) {
        /* P1102R2 (C++23): decl-specifiers, a noexcept-specifier,
           attributes or a trailing-return-type with no parameters */
        return beyond_this_step();
    }
    if (!at(preprocessing_op_or_punc::LEFT_BRACE)) {
        names.reenter(enclosing);
        return expected("'{' to begin the lambda's compound-statement",
                        "expr.prim.lambda");
    }
    compound_statement();
    names.reenter(enclosing);
    if (failed())
        return 0;
    return finish_node(syntax_kind::lambda_expression, mark, first);
}

node_index parser::lambda_introducer()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* [ */
    if (!at(preprocessing_op_or_punc::RIGHT_BRACKET)) {
        lambda_capture();
        if (failed())
            return 0;
    }
    if (!accept(preprocessing_op_or_punc::RIGHT_BRACKET))
        return expected("']' to end the lambda-introducer", "expr.prim.lambda");
    return finish_node(syntax_kind::lambda_introducer, mark, first);
}

/*
 * A capture-default is & or = alone: & before an identifier is a
 * simple-capture's or init-capture's.  One capture alone passes through,
 * and so does a capture-list alone.
 */
node_index parser::lambda_capture()
{
    size_t mark = begin_node();
    token_index first = position;
    bool capture_default = false;
    if (at(preprocessing_op_or_punc::EQUAL) ||
        (at(preprocessing_op_or_punc::AMPERSAND) &&
         (at(preprocessing_op_or_punc::COMMA, 1) ||
          at(preprocessing_op_or_punc::RIGHT_BRACKET, 1)))) {
        add_token();
        capture_default = true;
        if (!accept(preprocessing_op_or_punc::COMMA))
            return 0;
    }
    size_t list_mark = begin_node();
    token_index list_first = position;
    unsigned count = 0;
    for (;;) {
        capture();
        if (failed())
            return 0;
        count++;
        if (!accept(preprocessing_op_or_punc::COMMA))
            break;
    }
    if (count > 1)
        finish_node(syntax_kind::capture_list, list_mark, list_first);
    if (!capture_default)
        return 0;
    return finish_node(syntax_kind::lambda_capture, mark, first);
}

/*
 * Which capture it is follows from its first tokens: this, * this, & or an
 * identifier, then an initializer for an init-capture (=, ( or {).  An
 * identifier alone passes through.
 */
node_index parser::capture()
{
    const implementation_defined_behavior &b = tokens.files.behavior;
    size_t mark = begin_node();
    token_index first = position;
    if (at(standard_keyword::this_)) {
        add_token();
        return 0;
    }
    if (at(preprocessing_op_or_punc::STAR) && at(standard_keyword::this_, 1)) {
        if (!b.has(language_feature::star_this_capture))
            return beyond_this_step();
        add_token();
        add_token();
        return finish_node(syntax_kind::simple_capture, mark, first);
    }
    bool reference = accept(preprocessing_op_or_punc::AMPERSAND);
    /* init-capture: &_opt ... identifier initializer (P0780R2, C++20) */
    bool init_pack = false;
    if (at(preprocessing_op_or_punc::ELLIPSIS)) {
        if (!b.has(language_feature::init_capture_packs))
            return beyond_this_step();
        add_token();
        init_pack = true;
    }
    if (!at_identifier())
        return expected("a capture", "expr.prim.lambda.capture");
    uint32_t name = tokens.identifier(position);
    add_token();
    /* simple-capture ... : a pack expansion ([temp.variadic]/5) */
    if (!init_pack && at(preprocessing_op_or_punc::ELLIPSIS)) {
        add_token();
        return finish_node(syntax_kind::simple_capture, mark, first);
    }
    if (init_pack && !at(preprocessing_op_or_punc::EQUAL) &&
        !at(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
        !at(preprocessing_op_or_punc::LEFT_BRACE))
        return expected("an initializer after the init-capture pack",
                        "expr.prim.lambda.capture");
    if (at(preprocessing_op_or_punc::EQUAL) ||
        at(preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
        at(preprocessing_op_or_punc::LEFT_BRACE)) {
        if (!b.has(language_feature::init_captures))
            return beyond_this_step();
        initializer();
        if (failed())
            return 0;
        names.declare(name, name_kind::other);
        return finish_node(syntax_kind::init_capture, mark, first);
    }
    if (!reference)
        return 0;
    return finish_node(syntax_kind::simple_capture, mark, first);
}

/*
 * [expr.prim.lambda]/3: "In the decl-specifier-seq of the
 * lambda-declarator, each decl-specifier shall be one of mutable,
 * constexpr, or consteval."  A C++11 or C++14 lambda-declarator has an
 * exception-specification where C++20's has the noexcept-specifier
 * (except.spec.cpp reads both).  *parameters: the function parameter
 * scope, which the compound-statement continues.
 */
node_index parser::lambda_declarator(scope_index *parameters)
{
    const implementation_defined_behavior &b = tokens.files.behavior;
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* ( */
    scope_index enclosing = names.current();
    *parameters = names.enter(scope_kind::function_prototype);
    {
        /* [temp.res]/5: a parameter-declaration in a lambda-declarator */
        flag_set implicit(parameters_implicit_typename, true);
        parameter_declaration_clause();
    }
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to close the lambda's parameters",
                        "expr.prim.lambda");
    size_t specifiers_mark = begin_node();
    token_index specifiers_first = position;
    unsigned specifiers = 0;
    /* [dcl.spec]/2: "each decl-specifier shall appear at most once in a
       complete decl-specifier-seq" */
    bool seen_mutable = false, seen_constexpr = false, seen_consteval = false;
    for (;;) {
        bool *seen = nullptr;
        if (at(standard_keyword::mutable_)) {
            seen = &seen_mutable;
        } else if (at(standard_keyword::consteval_)) {
            seen = &seen_consteval;
        } else if (at(standard_keyword::constexpr_)) {
            if (!b.has(language_feature::constexpr_lambdas))
                return beyond_this_step();
            seen = &seen_constexpr;
        } else if (at(standard_keyword::static_)) {
            return beyond_this_step(); /* P1169R4 (C++23): static */
        } else {
            break;
        }
        if (*seen)
            return violates("each decl-specifier at most once", "dcl.spec",
                            position);
        *seen = true;
        add_token();
        specifiers++;
    }
    if (specifiers > 1)
        finish_node(syntax_kind::decl_specifier_seq, specifiers_mark,
                    specifiers_first);
    if (at(standard_keyword::noexcept_))
        noexcept_specifier();
    else if (at(standard_keyword::throw_))
        dynamic_exception_specification();
    if (failed())
        return 0;
    attribute_specifier_seq(true);
    if (failed())
        return 0;
    if (at(preprocessing_op_or_punc::ARROW)) {
        trailing_return_type();
        if (failed())
            return 0;
    }
    if (at(standard_keyword::requires_)) {
        requires_clause();
        if (failed())
            return 0;
    }
    names.reenter(enclosing);
    return finish_node(syntax_kind::lambda_declarator, mark, first);
}

} // namespace cplusplus
