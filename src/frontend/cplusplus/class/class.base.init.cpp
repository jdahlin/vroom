/*
 * class.base.init.cpp: [class.base.init] Initializing bases and members:
 * the ctor-initializer of a constructor's function-body
 * ([dcl.fct.def.general]: ctor-initializer_opt compound-statement).
 *
 *     ctor-initializer:
 *         : mem-initializer-list
 *
 *     mem-initializer-list:
 *         mem-initializer ..._opt
 *         mem-initializer-list , mem-initializer ..._opt
 *
 * [class.base.init]/2: "In a mem-initializer-id an initial unqualified
 * identifier is looked up in the scope of the constructor's class and, if
 * not found in that scope, in the scope containing the constructor's
 * definition."  The identifier is a member or a base: either way not an
 * expression, so what it names does not change the parse.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::ctor_initializer()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* : */
    if (!comma_list(syntax_kind::mem_initializer_list,
                    [this] { mem_initializer(); }))
        return 0;
    return finish_node(syntax_kind::ctor_initializer, mark, first);
}

/*
 *     mem-initializer:
 *         mem-initializer-id ( expression-list_opt )
 *         mem-initializer-id braced-init-list
 *
 *     mem-initializer-id:
 *         class-or-decltype
 *         identifier
 */
node_index parser::mem_initializer()
{
    size_t mark = begin_node();
    token_index first = position;
    scope_index base = 0;
    class_or_decltype(&base);
    if (failed())
        return 0;
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        if (!parenthesized_expression_list("')' to close the mem-initializer",
                                           "class.base.init"))
            return 0;
    } else if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
        if (list_initialization_missing())
            return 0;
        braced_init_list();
        if (failed())
            return 0;
    } else {
        return expected("'(' or '{' after the mem-initializer-id",
                        "class.base.init");
    }
    return finish_node(syntax_kind::mem_initializer, mark, first);
}

} // namespace cplusplus
