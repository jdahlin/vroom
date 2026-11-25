/*
 * asm-statement.cpp: [ext:gnu_asm_statements] (GCC manual 6.47, Using
 * Assembly Language with C): "The asm keyword allows you to embed
 * assembler instructions within C code."  Basic asm has a template alone,
 * extended asm gives the operands the template names:
 *
 *     statement:
 *         gnu-asm-statement
 *
 *     gnu-asm-statement:
 *         asm gnu-asm-qualifier-seq_opt ( string-literal ) ;
 *         asm gnu-asm-qualifier-seq_opt ( string-literal : gnu-asm-operand-list_opt ) ;
 *         asm gnu-asm-qualifier-seq_opt ( string-literal : gnu-asm-operand-list_opt : gnu-asm-operand-list_opt ) ;
 *         asm gnu-asm-qualifier-seq_opt ( string-literal : gnu-asm-operand-list_opt : gnu-asm-operand-list_opt : gnu-asm-clobber-list_opt ) ;
 *         asm gnu-asm-qualifier-seq_opt ( string-literal : gnu-asm-operand-list_opt : gnu-asm-operand-list_opt : gnu-asm-clobber-list_opt : gnu-asm-label-list_opt ) ;
 *
 *     gnu-asm-qualifier-seq:
 *         gnu-asm-qualifier-seq_opt gnu-asm-qualifier
 *
 *     gnu-asm-qualifier:
 *         volatile
 *         inline
 *         goto
 *
 *     gnu-asm-operand-list:
 *         gnu-asm-operand
 *         gnu-asm-operand-list , gnu-asm-operand
 *
 *     gnu-asm-operand:
 *         string-literal ( expression )
 *         [ identifier ] string-literal ( expression )
 *
 *     gnu-asm-clobber-list:
 *         string-literal
 *         gnu-asm-clobber-list , string-literal
 *
 *     gnu-asm-label-list:
 *         identifier
 *         gnu-asm-label-list , identifier
 *
 * The tree is flat: the tokens of the statement, with a gnu-asm-operand
 * node for each operand; the sections are told apart by their ':' tokens
 * ("::" is two, an empty section between them).  The string literals are
 * ordinary ones: clang rejects a wide or unicode one ("cannot use wide
 * string literal in 'asm'").  A qualifier given twice is GCC's "duplicate
 * asm qualifier".  The label section is asm goto's, and asm goto has one
 * with a label in it (clang: "expected ':'", "expected identifier").
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* an ordinary string-literal, one token */
bool parser::gnu_asm_string(const char *what)
{
    bool ordinary = peek().kind == token_kind::literal &&
                    peek().literal == literal_kind::string_literal;
    size_t count = 0;
    const preprocessing_token *piece = tokens.pieces(position, &count);
    for (size_t i = 0; ordinary && i < count; i++)
        ordinary = piece[i].prefix == encoding_prefix::none;
    if (!ordinary) {
        expected(what, "ext.gnu");
        return false;
    }
    add_token();
    return true;
}

/* [ symbolic-name ] constraint ( expression ) */
node_index parser::gnu_asm_operand()
{
    size_t mark = begin_node();
    token_index first = position;
    if (at(preprocessing_op_or_punc::LEFT_BRACKET)) {
        add_token();
        if (!at_identifier())
            return expected("a symbolic name for the asm operand", "ext.gnu");
        add_token();
        if (!accept(preprocessing_op_or_punc::RIGHT_BRACKET))
            return expected("']' after the symbolic name", "ext.gnu");
    }
    if (!gnu_asm_string("an ordinary string-literal, the constraint"))
        return 0;
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after the constraint", "ext.gnu");
    {
        flag_set greater(greater_is_operator, true);
        expression();
    }
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to end the asm operand", "ext.gnu");
    return finish_node(syntax_kind::gnu_asm_operand, mark, first);
}

node_index parser::gnu_asm_statement()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* asm */
    bool seen_volatile = false, seen_inline = false, seen_goto = false;
    for (;;) {
        bool *seen = nullptr;
        if (at(standard_keyword::volatile_))
            seen = &seen_volatile;
        else if (at(standard_keyword::inline_))
            seen = &seen_inline;
        else if (at(standard_keyword::goto_))
            seen = &seen_goto;
        if (!seen)
            break;
        if (*seen)
            return violates("an asm qualifier is given once", "ext.gnu",
                            position);
        *seen = true;
        add_token();
    }
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after asm", "ext.gnu");
    if (!gnu_asm_string("an ordinary string-literal, the assembler template"))
        return 0;
    /* the section each ':' begins: 1 outputs, 2 inputs, 3 clobbers, 4 labels */
    unsigned section = 0, labels = 0;
    for (;;) {
        unsigned colons = 0;
        if (at(preprocessing_op_or_punc::COLON))
            colons = 1;
        else if (at(preprocessing_op_or_punc::COLON_COLON))
            colons = 2;
        if (!colons)
            break;
        /* the labels are asm goto's alone */
        if (section + colons > 4 || (section + colons == 4 && !seen_goto))
            return expected("')' to end the asm statement", "ext.gnu");
        add_token();
        /* the section of the last ':'; "::" leaves the one before empty */
        section += colons;
        bool empty = at(preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
                     at(preprocessing_op_or_punc::COLON) ||
                     at(preprocessing_op_or_punc::COLON_COLON);
        if (empty)
            continue;
        do {
            if (section <= 2) {
                gnu_asm_operand();
                if (failed())
                    return 0;
            } else if (section == 3) {
                if (!gnu_asm_string("an ordinary string-literal, a clobber"))
                    return 0;
            } else {
                if (!at_identifier())
                    return expected("a label of the asm goto", "ext.gnu");
                add_token();
                labels++;
            }
        } while (accept(preprocessing_op_or_punc::COMMA));
    }
    /* asm goto names at least one label: clang's "expected ':'" and
       "expected identifier" */
    if (seen_goto && section < 4)
        return expected("':' to begin the labels of the asm goto", "ext.gnu");
    if (seen_goto && !labels)
        return expected("a label of the asm goto", "ext.gnu");
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to end the asm statement", "ext.gnu");
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the asm statement", "ext.gnu");
    return finish_node(syntax_kind::gnu_asm_statement, mark, first);
}

/* ---- semantic analysis ---------------------------------------------- */

/* the characters of the ordinary string-literal at t, escape sequences
   decoded (as code generation reads a template, a constraint, a clobber) */
void gnu_asm_string_value(token_stream &tokens, token_index t,
                          owned_buffer &out)
{
    const uint32_t *units = tokens.files.phase7().string_code_units();
    const literal_value &v = tokens.files.value_of(tokens[t]);
    for (uint32_t i = 0; i < v.string_length; i++) {
        char c = static_cast<char>(units[v.string_offset + i]);
        vbuffer_append(out.get(), &c, 1);
    }
}

/*
 * [ext:gnu_asm_statements] one operand: an output is a modifiable lvalue
 * whose constraint begins with = or +; an input is a prvalue (an array or
 * function decays), or an lvalue left as it is, for the code generator to
 * read or to give in memory; its constraint begins with neither.  Which
 * constraints a target has, and whether the operand's type suits one, is
 * the code generator's to say.
 */
analysis_result semantic_analysis::gnu_asm_operand(GnuAsmOperand n, bool output,
                                                   uint32_t &built)
{
    uint32_t i = 0;
    token_index name = 0;
    if (n[i].is(preprocessing_op_or_punc::LEFT_BRACKET)) {
        name = n[i + 1].token();
        i += 3;
    }
    token_index constraint = n[i].token();
    owned_buffer written;
    gnu_asm_string_value(tokens, constraint, written);
    bool modifier = written.size() &&
                    (written.data()[0] == '=' || written.data()[0] == '+');
    if (output && !modifier)
        return ill_formed(n, "an asm output constraint begins with '=' or '+'",
                          "ext.gnu");
    if (!output && modifier)
        return ill_formed(n,
                          "an asm input constraint begins with neither '=' "
                          "nor '+'",
                          "ext.gnu");
    expression_meaning m;
    VTRY(r, expression(n[i + 2], m));
    if (!m.node || m.overload_name)
        return not_supported_because(
            n, "ext.gnu",
            "an asm operand that has no value or names an overload set");
    if (depends_on_template_parameter(m.type))
        return not_supported_because(n, "temp.dep.constexpr",
                                     "an asm operand of dependent type");
    if (output) {
        /* [basic.lval]/10: an lvalue is modifiable unless its type is
           const-qualified or is a function type; clang: "invalid lvalue in
           asm output" */
        if (m.category != value_category::lvalue || (m.type.cv() & cv_const) ||
            type_table[m.type.unqualified()].kind == type_kind::function)
            return ill_formed(n, "an asm output shall be a modifiable lvalue",
                              "ext.gnu");
        /* the write back into a bit-field is not generated */
        if (m.bit_field_width)
            return not_supported_because(
                n, "ext.gnu", "a bit-field as an asm output operand");
    } else {
        type_kind k = type_table[m.type.unqualified()].kind;
        if (k == type_kind::array || k == type_kind::function)
            VTRY_SET(r, operand_value(n, m));
    }
    built = expression_node(semantic_kind::gnu_asm_operand, m, constraint,
                            m.node, name);
    semantics[built].value = output;
    return analysis_result::well_formed;
}

/*
 * [ext:gnu_asm_statements]: a statement, which names its operands to the
 * code generator.  Two symbolic names of one statement are one error (clang:
 * "duplicate symbolic operand name").  asm goto's labels are recorded, not
 * lowered.
 */
analysis_result semantic_analysis::gnu_asm_statement(GnuAsmStatement n,
                                                     uint32_t &built)
{
    uint64_t flags = 0;
    uint32_t i = 1;
    for (; i < n.size() && !n[i].is(preprocessing_op_or_punc::LEFT_PARENTHESIS);
         i++) {
        switch (n[i].keyword()) {
        case standard_keyword::volatile_:
            flags |= 1;
            break;
        case standard_keyword::inline_:
            flags |= 2;
            break;
        default:
            flags |= 4;
            break;
        }
    }
    if (i + 1 >= n.size())
        return not_supported_because(
            n, "ext.gnu",
            "an asm statement that ends before its template string");
    token_index tmpl = n[i + 1].token();
    owned_array<uint32_t> operands, clobbers, labels;
    uint32_t outputs = 0;
    unsigned section = 0;
    analysis_result result = analysis_result::well_formed;
    for (i += 2; i < n.size(); i++) {
        SyntaxElement c = n[i];
        if (c.is(preprocessing_op_or_punc::COLON)) {
            section += 1;
        } else if (c.is(preprocessing_op_or_punc::COLON_COLON)) {
            section += 2;
        } else if (c.is_node()) {
            uint32_t operand = 0;
            analysis_result r =
                gnu_asm_operand(GnuAsmOperand(c), section == 1, operand);
            if (r != analysis_result::well_formed) {
                /* the operands after it are analysed: their errors too */
                if (result == analysis_result::well_formed ||
                    r == analysis_result::ill_formed)
                    result = r;
                continue;
            }
            operands.append(operand);
            if (section == 1)
                outputs++;
        } else if (section == 3 && c.is_token() &&
                   !c.is(preprocessing_op_or_punc::COMMA) &&
                   !c.is(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
            clobbers.append(c.token());
        } else if (section == 4 && c.is_identifier()) {
            labels.append(c.token());
        }
    }
    if (result != analysis_result::well_formed)
        return result;
    if (labels.size())
        flags |= 4;
    if (section)
        flags |= 8;
    /* symbolic names are unique among the operands */
    for (size_t a = 0; a < operands.size(); a++) {
        token_index name = semantics[operands[a]].b;
        for (size_t b = 0; name && b < a; b++) {
            token_index other = semantics[operands[b]].b;
            if (other && tokens.identifier(other) == tokens.identifier(name))
                return ill_formed(n, "duplicate symbolic operand name",
                                  "ext.gnu");
        }
    }
    built = statement_node(semantic_kind::gnu_asm_statement, tmpl, outputs);
    semantic_node &made = semantics[built];
    made.value = flags | (static_cast<uint64_t>(labels.size()) << 8);
    semantics.set_list(built, operands.data(), operands.size());
    if (clobbers.size()) {
        made.b = semantics.list(clobbers.data(), clobbers.size());
        made.c = static_cast<uint32_t>(clobbers.size());
    }
    if (labels.size())
        made.d = semantics.list(labels.data(), labels.size());
    return analysis_result::well_formed;
}

} // namespace cplusplus
