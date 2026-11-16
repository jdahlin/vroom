/*
 * expr.post.cpp: [expr.post] Postfix expressions, with the subclauses
 * that give its alternatives their meaning: [expr.sub], [expr.call],
 * [expr.type.conv], [expr.ref], [expr.post.incr], [expr.dynamic.cast],
 * [expr.typeid], [expr.static.cast], [expr.reinterpret.cast],
 * [expr.const.cast].
 *
 *     postfix-expression:
 *         primary-expression
 *         postfix-expression [ expr-or-braced-init-list ]
 *         postfix-expression ( expression-list_opt )
 *         simple-type-specifier ( expression-list_opt )
 *         typename-specifier ( expression-list_opt )
 *         simple-type-specifier braced-init-list
 *         typename-specifier braced-init-list
 *         postfix-expression . template_opt id-expression
 *         postfix-expression -> template_opt id-expression
 *         postfix-expression ++
 *         postfix-expression --
 *         dynamic_cast < type-id > ( expression )
 *         static_cast < type-id > ( expression )
 *         reinterpret_cast < type-id > ( expression )
 *         const_cast < type-id > ( expression )
 *         typeid ( expression )
 *         typeid ( type-id )
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * [expr.post]/1: "Postfix expressions group left-to-right": what begins
 * one (a primary-expression, a type with its initializer, a named cast,
 * typeid) is the operand of the first suffix, and each suffix makes the
 * operand of the next.  With no suffix, the primary-expression passes
 * through.
 *
 * [expr.type.conv]/1: "A simple-type-specifier or typename-specifier
 * followed by a parenthesized optional expression-list or by a
 * braced-init-list (the initializer) constructs a value of the specified
 * type given the initializer."  A name begins this alternative when lookup
 * finds a type; otherwise it is a primary-expression.
 *
 * typename-specifier is P7's, as are the template after . and -> and a
 * member's template-id; a placeholder for a deduced type (auto(x),
 * P0849R8, C++23) is not n4861's.
 */
node_index parser::postfix_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    node_index n = 0;
    standard_keyword k = keyword_of(peek());
    bool type = false;
    switch (k) {
    case standard_keyword::dynamic_cast_:
    case standard_keyword::static_cast_:
    case standard_keyword::reinterpret_cast_:
    case standard_keyword::const_cast_:
        add_token();
        if (!accept(preprocessing_op_or_punc::LESS))
            return expected("'<' after the cast's keyword", "expr.post");
        type_id();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::GREATER))
            return expected("'>' after the type-id", "expr.post");
        if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
            return expected("'(' before the cast's operand", "expr.post");
        expression();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("')' after the cast's operand", "expr.post");
        n = finish_node(syntax_kind::postfix_expression, mark, first);
        break;
    case standard_keyword::typeid_:
        add_token();
        if (!at(preprocessing_op_or_punc::LEFT_PARENTHESIS))
            return expected("'(' after typeid", "expr.typeid");
        /* typeid ( type-id ) when the operand can be one
           ([dcl.ambig.res]/2) */
        if (!parenthesized_type_id(false)) {
            if (failed())
                return 0;
            add_token();
            expression();
            if (failed())
                return 0;
            if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                return expected("')' after the operand of typeid",
                                "expr.typeid");
        }
        n = finish_node(syntax_kind::postfix_expression, mark, first);
        break;
    case standard_keyword::auto_:
    case standard_keyword::typename_:
        return beyond_this_step();
    case standard_keyword::decltype_:
        /* decltype-specifier :: begins a qualified-id */
        if (!at(preprocessing_op_or_punc::COLON_COLON, group_end(1))) {
            if (at(standard_keyword::auto_, 2))
                return beyond_this_step(); /* decltype(auto)(x): P0849R8 */
            decltype_specifier();
            type = true;
        }
        break;
    default:
        if (is_simple_type_keyword(k)) {
            add_token();
            type = true;
        } else if (at_identifier() ||
                   at(preprocessing_op_or_punc::COLON_COLON)) {
            unsigned length = 0;
            if (classify_name(0, &length, false) == name_class::type) {
                if (at(preprocessing_op_or_punc::LESS, length))
                    return beyond_this_step(); /* a template-id: P7 */
                simple_type_specifier_with_name();
                type = true;
            }
        }
        break;
    }
    if (failed())
        return 0;
    if (type) {
        if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
            add_token();
            if (!at(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
                expression_list();
                if (failed())
                    return 0;
            }
            if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                return expected("')' to close the expression-list",
                                "expr.type.conv");
        } else if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
            if (list_initialization_missing())
                return 0;
            braced_init_list();
            if (failed())
                return 0;
        } else {
            return expected("'(' or a braced-init-list after the "
                            "simple-type-specifier",
                            "expr.type.conv");
        }
        n = finish_node(syntax_kind::postfix_expression, mark, first);
    } else if (!n) {
        n = primary_expression();
        if (failed())
            return 0;
    }

    const implementation_defined_behavior &b = tokens.files.behavior;
    for (;;) {
        if (at(preprocessing_op_or_punc::LEFT_BRACKET)) {
            /* [dcl.attr.grammar]/7: "Two consecutive left square bracket
               tokens shall appear only when introducing an
               attribute-specifier": a[[]{ return 0; }()] is ill-formed */
            if (at(preprocessing_op_or_punc::LEFT_BRACKET, 1))
                return misplaced_attribute_brackets();
            add_token();
            if (b.has(language_feature::multidimensional_subscript)) {
                /* P2128R6 (C++23):
                       postfix-expression [ expression-list_opt ] */
                if (!at(preprocessing_op_or_punc::RIGHT_BRACKET))
                    expression_list();
            } else if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
                if (!list_initialization_missing())
                    braced_init_list();
            } else {
                expression();
            }
            if (failed())
                return 0;
            if (!accept(preprocessing_op_or_punc::RIGHT_BRACKET))
                return expected("']' to close the subscript", "expr.sub");
        } else if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
            add_token();
            if (!at(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
                expression_list();
                if (failed())
                    return 0;
            }
            if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                return expected("')' to close the expression-list",
                                "expr.call");
        } else if (at(preprocessing_op_or_punc::DOT) ||
                   at(preprocessing_op_or_punc::ARROW)) {
            add_token();
            id_expression(true);
            if (failed())
                return 0;
        } else if (at(preprocessing_op_or_punc::PLUS_PLUS) ||
                   at(preprocessing_op_or_punc::MINUS_MINUS)) {
            add_token();
        } else {
            break;
        }
        n = finish_node(syntax_kind::postfix_expression, mark, first);
    }
    return n;
}

/*
 *     expression-list:
 *         initializer-list
 */
node_index parser::expression_list()
{
    return initializer_list(false);
}

} // namespace cplusplus
