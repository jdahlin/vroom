/*
 * stmt.label.cpp: [stmt.label] Labeled statement.
 *
 *     labeled-statement:
 *         attribute-specifier-seq_opt identifier : statement
 *         attribute-specifier-seq_opt case constant-expression : statement
 *         attribute-specifier-seq_opt default : statement
 *
 * [stmt.label]/1: "The optional attribute-specifier-seq appertains to the
 * label."  P2324R2 (C++23) lets a label end a compound-statement; before
 * it that is [ext.clang] clang's extension, P9's.
 */
#include "stmt.ambig.h"

namespace cplusplus
{

node_index parser::labeled_statement()
{
    size_t mark = begin_node();
    token_index first = position;
    attribute_specifier_seq();
    if (failed())
        return 0;
    if (accept(standard_keyword::case_)) {
        constant_expression();
        if (failed())
            return 0;
        if (at(preprocessing_op_or_punc::ELLIPSIS))
            /* [ext.gnu] a case range, case 1 ... 3: P9 */
            return beyond_this_step();
    } else {
        add_token(); /* default, or the identifier */
    }
    if (!accept(preprocessing_op_or_punc::COLON))
        return expected("':' after the label", "stmt.label");
    if (at(preprocessing_op_or_punc::RIGHT_BRACE)) {
        if (!tokens.files.behavior.has(language_feature::labels_anywhere))
            return beyond_this_step();
    } else {
        statement();
        if (failed())
            return 0;
    }
    return finish_node(syntax_kind::labeled_statement, mark, first);
}

} // namespace cplusplus
