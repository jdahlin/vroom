/*
 * except.spec.cpp: [except.spec] Exception specifications, as far as a
 * declarator needs them.
 *
 *     noexcept-specifier:
 *         noexcept ( constant-expression )
 *         noexcept
 *
 * The constant-expression is P3's: kept as its tokens.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::noexcept_specifier()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token();
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        unparsed_group();
        if (failed())
            return 0;
    }
    return finish_node(syntax_kind::noexcept_specifier, mark, first);
}

} // namespace cplusplus
