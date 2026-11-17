/*
 * type_traits.cpp: [ext:clang_type_traits] (clang Language Extensions,
 * Type Trait Primitives): the compiler's answers to <type_traits>'
 * questions, which libstdc++ and libc++ use when __has_builtin says they
 * exist.
 *
 *     primary-expression:
 *         clang-type-trait-expression
 *
 *     clang-type-trait-expression:
 *         clang-type-trait ( clang-type-trait-argument-list )
 *
 *     clang-type-trait-argument-list:
 *         clang-type-trait-argument
 *         clang-type-trait-argument-list , clang-type-trait-argument
 *
 *     clang-type-trait-argument:
 *         type-id ..._opt
 *         assignment-expression ..._opt
 *
 *     simple-type-specifier:
 *         clang-type-transformation
 *
 *     clang-type-transformation:
 *         clang-type-transformation-keyword ( type-id )
 *
 * The keywords are extension-keywords.inc's type_trait and
 * type_transformation rows.  Most traits take type-ids; __array_extent's
 * second argument, __is_lvalue_expr's and __is_rvalue_expr's are
 * expressions: an argument is a type-id when the tokens can be one
 * ([dcl.ambig.res]/2's rule).  How many arguments each takes is not
 * grammar.  With clang_type_trait_names the keyword is an identifier when
 * no '(' follows (parse/token_stream.cpp).
 */
#include "../../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::clang_type_trait_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* the trait */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after the type trait", "ext.clang");
    flag_set greater(greater_is_operator, true);
    if (!at(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
        for (;;) {
            bool type = false;
            {
                tentative_parse attempt(*this);
                type_id();
                if (!failed() &&
                    (at(preprocessing_op_or_punc::COMMA) ||
                     at(preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
                     at(preprocessing_op_or_punc::ELLIPSIS))) {
                    attempt.commit();
                    type = true;
                }
            }
            if (!type) {
                assignment_expression();
                if (failed())
                    return 0;
            }
            accept(preprocessing_op_or_punc::ELLIPSIS);
            if (!accept(preprocessing_op_or_punc::COMMA))
                break;
        }
    }
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to end the type trait's arguments", "ext.clang");
    return finish_node(syntax_kind::clang_type_trait_expression, mark, first);
}

node_index parser::clang_type_transformation()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* the transformation */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after the type transformation", "ext.clang");
    flag_set greater(greater_is_operator, true);
    type_id();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to end the type transformation", "ext.clang");
    return finish_node(syntax_kind::clang_type_transformation, mark, first);
}

} // namespace cplusplus
