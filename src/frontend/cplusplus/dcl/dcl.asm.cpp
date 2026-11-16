/*
 * dcl.asm.cpp: [dcl.asm] The asm declaration.
 *
 *     asm-declaration:
 *         attribute-specifier-seq_opt asm ( string-literal ) ;
 *
 * [dcl.asm]/1: "The asm declaration is conditionally-supported; its
 * meaning is implementation-defined."  GNU's asm with operands and an asm
 * label after a declarator are extensions (P9).
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::asm_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    attribute_specifier_seq();
    if (failed())
        return 0;
    if (!accept(standard_keyword::asm_))
        return expected("'asm'", "dcl.asm");
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after asm", "dcl.asm");
    if (peek().literal != literal_kind::string_literal)
        return expected("a string-literal", "dcl.asm");
    add_token();
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' after the string-literal", "dcl.asm");
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the asm-declaration", "dcl.asm");
    return finish_node(syntax_kind::asm_declaration, mark, first);
}

} // namespace cplusplus
