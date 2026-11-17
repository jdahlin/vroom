/*
 * asm_label.cpp: [ext:gnu_asm_labels] (GCC manual 6.47.4, Controlling
 * Names Used in Assembler Code): "You can specify the name to be used in
 * the assembler code for a C function or variable by writing the asm (or
 * __asm__) keyword after the declarator."  GCC's attribute-specifiers may
 * follow it ("immediately before the comma, = or semicolon terminating the
 * declaration"):
 *
 *     declarator:
 *         ptr-declarator gnu-asm-label gnu-attribute-specifier-seq_opt
 *         noptr-declarator parameters-and-qualifiers trailing-return-type gnu-asm-label gnu-attribute-specifier-seq_opt
 *
 *     gnu-asm-label:
 *         asm ( string-literal )
 *
 * Only after a declarator that has a declarator-id: an abstract-declarator
 * names nothing to give an assembler name to.
 */
#include "../../cplusplus-internal.h"

namespace cplusplus
{

bool parser::gnu_declarator_suffix_follows(const declarator_facts &facts)
{
    if (!facts.has_id)
        return false;
    if (at(standard_keyword::asm_) &&
        at(preprocessing_op_or_punc::LEFT_PARENTHESIS, 1) &&
        has(extension::gnu_asm_labels))
        return true;
    return at_gnu_attribute();
}

node_index parser::gnu_declarator_suffix(size_t mark, token_index first,
                                         declarator_facts &facts)
{
    if (at(standard_keyword::asm_) && has(extension::gnu_asm_labels)) {
        size_t label_mark = begin_node();
        token_index label_first = position;
        add_token();
        if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
            return expected("'(' after asm", "ext.gnu");
        /* clang: "cannot use wide string literal in 'asm'", nor a unicode
           one, nor one with a ud-suffix */
        bool ordinary = peek().kind == token_kind::literal &&
                        peek().literal == literal_kind::string_literal;
        size_t count = 0;
        const preprocessing_token *piece = tokens.pieces(position, &count);
        for (size_t i = 0; ordinary && i < count; i++)
            ordinary = piece[i].prefix == encoding_prefix::none;
        if (!ordinary)
            return expected("an ordinary string-literal, the assembler name",
                            "ext.gnu");
        add_token();
        if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("')' to end the asm label", "ext.gnu");
        finish_node(syntax_kind::gnu_asm_label, label_mark, label_first);
    }
    gnu_attributes_opt();
    if (failed())
        return 0;
    facts.node = finish_node(syntax_kind::gnu_declarator, mark, first);
    return facts.node;
}

} // namespace cplusplus
