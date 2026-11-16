/*
 * dcl.init.cpp: [dcl.init] Initializers.
 *
 *     initializer:
 *         brace-or-equal-initializer
 *         ( expression-list )
 *
 *     brace-or-equal-initializer:
 *         = initializer-clause
 *         braced-init-list
 *
 * [dcl.init]/1: "The process of initialization described in this subclause
 * applies to all initializations regardless of syntactic context,
 * including the initialization of a function parameter, the initialization
 * of a return value, or when an initializer follows a declarator."
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * initializer_opt: nothing (0, no failure) when no initializer starts
 * here.  A braced-init-list passes through as its own node.
 */
node_index parser::initializer()
{
    size_t mark = begin_node();
    token_index first = position;
    if (at(preprocessing_op_or_punc::EQUAL)) {
        add_token();
        initializer_clause();
        if (failed())
            return 0;
        return finish_node(syntax_kind::brace_or_equal_initializer, mark,
                           first);
    }
    if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
        if (list_initialization_missing())
            return 0;
        return braced_init_list();
    }
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        add_token();
        if (at(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("an expression-list in the initializer",
                            "dcl.init");
        expression_list();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("')' to close the initializer", "dcl.init");
        return finish_node(syntax_kind::initializer, mark, first);
    }
    return 0;
}

/*
 * N2672: list-initialization is C++11's.  Before it
 * a braced list is only the initializer-clause of an aggregate's
 * initializer (= { ... }, and the lists nested in one); anywhere else
 * (T x{...}, T{...}, new T{...}, an argument, a default argument, the
 * right operand of an assignment) it is [ext.clang] clang's extension of
 * C++03, which is P9's.
 */
bool parser::list_initialization_missing()
{
    if (!at(preprocessing_op_or_punc::LEFT_BRACE) ||
        tokens.files.behavior.has(language_feature::list_initialization))
        return false;
    beyond_this_step();
    return true;
}

/*
 *     initializer-clause:
 *         assignment-expression
 *         braced-init-list
 *
 *     expr-or-braced-init-list:
 *         expression
 *         braced-init-list
 *
 * Both alternatives pass their operand on.
 */
node_index parser::initializer_clause()
{
    if (at(preprocessing_op_or_punc::LEFT_BRACE))
        return braced_init_list();
    return assignment_expression();
}

/*
 *     braced-init-list:
 *         { initializer-list ,_opt }
 *         { designated-initializer-list ,_opt }
 *         { }
 *
 * [dcl.init.list]/1: "List-initialization is initialization of an object
 * or reference from a braced-init-list. ... An initializer list may be
 * empty."  A '.' begins a designator, and nothing else in an
 * initializer-clause does, so the first token says which list it is.
 */
node_index parser::braced_init_list()
{
    nesting_level level(*this);
    if (!nesting_within_limit("an initializer nested no deeper than this "
                              "implementation's limit"))
        return 0;
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* { */
    if (!at(preprocessing_op_or_punc::RIGHT_BRACE)) {
        if (at(preprocessing_op_or_punc::DOT)) {
            /* P0329R4 (C++20); before it [ext.clang] C99's designators,
               clang's extension: P9 */
            if (!tokens.files.behavior.has(
                    language_feature::designated_initializers))
                return beyond_this_step();
            designated_initializer_list();
        } else {
            initializer_list(true);
        }
        if (failed())
            return 0;
        accept(preprocessing_op_or_punc::COMMA);
    }
    if (!accept(preprocessing_op_or_punc::RIGHT_BRACE))
        return expected("'}' to close the braced-init-list", "dcl.init");
    return finish_node(syntax_kind::braced_init_list, mark, first);
}

/*
 *     initializer-list:
 *         initializer-clause ..._opt
 *         initializer-list , initializer-clause ..._opt
 *
 * The clauses of the list are one node's children; a list of one clause
 * passes it on.  In braces, a ',' before the '}' is the braced-init-list's.
 * The ... of a pack expansion ([temp.variadic]) is P7's.
 */
node_index parser::initializer_list(bool in_braces)
{
    size_t mark = begin_node();
    token_index first = position;
    unsigned count = 0;
    node_index n = 0;
    for (;;) {
        if (in_braces && (at(preprocessing_op_or_punc::DOT) ||
                          at(preprocessing_op_or_punc::LEFT_BRACKET)))
            /* [ext.clang] C99's designators in C++: an initializer-list
               that mixes clauses with designators, the array designator
               [n] = (which here begins a lambda-expression, P6's): P9 */
            return beyond_this_step();
        if (!in_braces && list_initialization_missing())
            return 0;
        n = initializer_clause();
        if (failed())
            return 0;
        if (at(preprocessing_op_or_punc::ELLIPSIS))
            return beyond_this_step();
        count++;
        if (!at(preprocessing_op_or_punc::COMMA))
            break;
        if (in_braces && at(preprocessing_op_or_punc::RIGHT_BRACE, 1))
            break;
        add_token();
    }
    if (count < 2)
        return n;
    return finish_node(syntax_kind::initializer_list, mark, first);
}

/*
 *     designated-initializer-list:
 *         designated-initializer-clause
 *         designated-initializer-list , designated-initializer-clause
 *
 *     designated-initializer-clause:
 *         designator brace-or-equal-initializer
 *
 *     designator:
 *         . identifier
 *
 * As for initializer-list, a list of one clause passes it on and the ','
 * before the '}' is the braced-init-list's.
 */
node_index parser::designated_initializer_list()
{
    size_t mark = begin_node();
    token_index first = position;
    unsigned count = 0;
    node_index n = 0;
    for (;;) {
        size_t clause_mark = begin_node();
        token_index clause_first = position;
        if (!at(preprocessing_op_or_punc::DOT))
            /* [ext.clang] a clause without a designator after one with
               (C99's mixture, which clang accepts with a warning), or its
               array designator [n] =: the grammar has no such list.  P9 */
            return beyond_this_step();
        add_token();
        if (!at_identifier())
            return expected("an identifier after '.'", "dcl.init");
        add_token();
        finish_node(syntax_kind::designator, clause_mark, clause_first);
        if (at(preprocessing_op_or_punc::DOT) ||
            at(preprocessing_op_or_punc::LEFT_BRACKET))
            /* [ext.clang] C99's nested designator .a.b = and .a[n] =: P9 */
            return beyond_this_step();
        if (at(preprocessing_op_or_punc::EQUAL)) {
            size_t initializer_mark = begin_node();
            token_index initializer_first = position;
            add_token();
            initializer_clause();
            if (failed())
                return 0;
            finish_node(syntax_kind::brace_or_equal_initializer,
                        initializer_mark, initializer_first);
        } else if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
            braced_init_list();
            if (failed())
                return 0;
        } else {
            return expected("a brace-or-equal-initializer after the "
                            "designator",
                            "dcl.init");
        }
        n = finish_node(syntax_kind::designated_initializer_clause, clause_mark,
                        clause_first);
        count++;
        if (!at(preprocessing_op_or_punc::COMMA) ||
            at(preprocessing_op_or_punc::RIGHT_BRACE, 1))
            break;
        add_token();
    }
    if (count < 2)
        return n;
    return finish_node(syntax_kind::designated_initializer_list, mark, first);
}

} // namespace cplusplus
