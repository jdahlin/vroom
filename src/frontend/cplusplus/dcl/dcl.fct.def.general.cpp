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
        bool deleted = at(standard_keyword::delete_);
        add_token(); /* default or delete */
        /* C++26's
               = delete deleted-function-message_opt ;   (P2573R2)
           with deleted-function-message: ( string-literal ); in an earlier
           edition [ext.clang] clang_deleted_function_message */
        const implementation_defined_behavior &b = tokens.files.behavior;
        if (deleted && at(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
            (b.has(language_feature::deleted_function_message) ||
             b.has(extension::clang_deleted_function_message))) {
            add_token();
            if (peek().literal != literal_kind::string_literal)
                return expected("a string-literal", "dcl.fct.def.delete");
            add_token();
            if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                return expected("')'", "dcl.fct.def.delete");
        }
        if (!accept(preprocessing_op_or_punc::SEMICOLON))
            return expected("';' after the function-body",
                            "dcl.fct.def.general");
    }
    return finish_node(syntax_kind::function_body, mark, first);
}

} // namespace cplusplus
