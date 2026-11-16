/*
 * extension_keyword.cpp: [ext.gnu] gnu_extension_keyword (GCC manual 6.48,
 * Alternate Keywords): "-pedantic and other options cause warnings for many
 * GNU C extensions.  You can prevent such warnings within one expression by
 * writing __extension__ before the expression."  GCC and clang take it
 * before a declaration as well, at namespace, class and block scope.
 *
 *     __extension__ declaration
 *     __extension__ member-declaration
 *     __extension__ block-declaration
 *     __extension__ cast-expression
 *
 * The keyword changes no meaning: only which diagnostics are issued.
 */
#include "../../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::gnu_extension_prefixed(node_index (parser::*alternative)())
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* __extension__ */
    (this->*alternative)();
    if (failed())
        return 0;
    return finish_node(syntax_kind::gnu_extension_declaration, mark, first);
}

node_index parser::gnu_extension_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* __extension__ */
    cast_expression();
    if (failed())
        return 0;
    return finish_node(syntax_kind::gnu_extension_expression, mark, first);
}

} // namespace cplusplus
