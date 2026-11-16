/*
 * dcl.init.cpp: [dcl.init] Initializers, as far as a declaration needs to
 * find where one ends.
 *
 *     initializer:
 *         brace-or-equal-initializer
 *         ( expression-list )
 *
 *     brace-or-equal-initializer:
 *         = initializer-clause
 *         braced-init-list
 *
 * The initializer-clause, braced-init-list and expression-list are P3's:
 * kept as their tokens.  An initializer-clause after '=' ends at a ',' or
 * ';' outside all brackets.  Nothing when no initializer starts here.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::initializer()
{
    size_t mark = begin_node();
    token_index first = position;
    if (at(preprocessing_op_or_punc::EQUAL)) {
        add_token();
        static constexpr preprocessing_op_or_punc stops[] = {
            preprocessing_op_or_punc::COMMA,
            preprocessing_op_or_punc::SEMICOLON};
        if (!unparsed_until(stops, 2))
            return expected("an initializer-clause after '='", "dcl.init");
        return finish_node(syntax_kind::brace_or_equal_initializer, mark,
                           first);
    }
    if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
        /* braced-init-list: it passes through */
        unparsed_group();
        return 0;
    }
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        unparsed_group();
        if (failed())
            return 0;
        return finish_node(syntax_kind::initializer, mark, first);
    }
    return 0;
}

} // namespace cplusplus
