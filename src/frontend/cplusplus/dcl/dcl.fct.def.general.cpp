/*
 * dcl.fct.def.general.cpp: [dcl.fct.def.general] Function definitions: the
 * function-body.  The function-definition itself is parsed with the
 * simple-declaration it shares its beginning with (dcl.pre.cpp).
 *
 *     function-body:
 *         ctor-initializer_opt compound-statement
 *         function-try-block
 *         = default ;
 *         = delete ;
 *
 * The compound-statement is P4's: kept as its tokens.  The ctor-initializer
 * is P5's and the function-try-block P6's.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::function_body()
{
    size_t mark = begin_node();
    token_index first = position;
    if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
        unparsed_group();
        if (failed())
            return 0;
    } else {
        add_token(); /* = */
        add_token(); /* default or delete */
        if (!accept(preprocessing_op_or_punc::SEMICOLON))
            return expected("';' after the function-body",
                            "dcl.fct.def.general");
    }
    return finish_node(syntax_kind::function_body, mark, first);
}

} // namespace cplusplus
