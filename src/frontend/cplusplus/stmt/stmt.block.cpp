/*
 * stmt.block.cpp: [stmt.block] Compound statement or block.
 *
 *     compound-statement:
 *         { statement-seq_opt }
 *
 *     statement-seq:
 *         statement
 *         statement-seq statement
 *
 * [basic.scope.block]/1: "A name declared in a block ([stmt.block]) is
 * local to that block; it has block scope."  A statement-seq of one
 * statement passes it on.
 */
#include "stmt.ambig.h"

namespace cplusplus
{

node_index parser::compound_statement()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* { */
    declarative_region block(names, scope_kind::block);
    size_t sequence_mark = begin_node();
    token_index sequence_first = position;
    unsigned count = 0;
    while (!at(preprocessing_op_or_punc::RIGHT_BRACE) && !at_end()) {
        statement();
        if (failed())
            return 0;
        count++;
    }
    if (count > 1)
        finish_node(syntax_kind::statement_seq, sequence_mark, sequence_first);
    if (!accept(preprocessing_op_or_punc::RIGHT_BRACE))
        return expected("'}' to end the compound-statement", "stmt.block");
    return finish_node(syntax_kind::compound_statement, mark, first);
}

} // namespace cplusplus
