/*
 * compound-statement.cpp: [stmt.block] Compound statement or block.
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
#include "frontend/cplusplus/sema.h"
#include "ambiguity-resolution.h"

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

/* ---- semantic analysis ---------------------------------------------- */

/*
 * [stmt.block]/1: "A compound statement (also known as a block) groups a
 * sequence of statements into a single statement."  [basic.scope.block]/1:
 * "A name declared in a block is local to that block".
 */
analysis_result semantic_analysis::compound_statement(CompoundStatement n,
                                                      entity_index conflicts,
                                                      uint32_t &built)
{
    entity_index outer = current_scope;
    open_scope(conflicts, false);
    analysis_result r = block_items(n, built);
    current_scope = outer;
    return r;
}

/* the statements of a block, each with the place for the claims about its
   expressions just after it */
analysis_result semantic_analysis::block_items(SyntaxRef n, uint32_t &built)
{
    owned_array<SyntaxElement> items;
    for (auto c : n.elements()) {
        if (c.is(preprocessing_op_or_punc::LEFT_BRACE,
                 preprocessing_op_or_punc::RIGHT_BRACE))
            continue;
        if (c.is(syntax_kind::statement_seq)) {
            for (auto child : c.elements())
                items.append(child);
            continue;
        }
        items.append(c);
    }
    analysis_result result = analysis_result::well_formed;
    token_index place = claim_place;
    entity_index place_scope = claim_scope;
    owned_array<uint32_t> statements;
    for (size_t i = 0; i < items.size(); i++) {
        SyntaxElement s = items[i];
        claim_place = s.token();
        if (!s.is_token())
            claim_place = s.end_token() - 1;
        claim_scope = current_scope;
        uint32_t one = 0;
        /* [dcl.attr.fallthrough]/1: "The next statement that would be
           executed after a fallthrough statement shall be a labeled
           statement whose label is a case label or default label for the
           same switch statement": the next one of the block, else not
           analysed (clang: "fallthrough annotation does not directly
           precede switch label") */
        if (fallthrough_statement(s) &&
            (i + 1 == items.size() || !case_or_default_label(items[i + 1]))) {
            result = worse(
                result, not_supported_because(
                            s, "dcl.attr.fallthrough",
                            "a fallthrough not directly before a case label"));
            statements.append(one);
            continue;
        }
        result = worse(result, statement(s, one));
        statements.append(one);
    }
    claim_place = place;
    claim_scope = place_scope;
    built = block_statement(n.first_token(), statements);
    return result;
}

} // namespace cplusplus
