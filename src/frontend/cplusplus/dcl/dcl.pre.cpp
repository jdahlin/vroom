/*
 * dcl.pre.cpp: [dcl.pre] Declarations: preamble.
 *
 *     declaration-seq:
 *         declaration
 *         declaration-seq declaration
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/* declaration-seq, or no node when there is no declaration (the callers
   have declaration-seq_opt) */
node_index parser::declaration_seq()
{
    if (at_end())
        return 0;
    size_t mark = begin_node();
    token_index first = position;
    while (!at_end())
        add(declaration());
    return finish_node(syntax_kind::declaration_seq, mark, first);
}

/* [dcl.pre]/1's declaration: none of its alternatives is written yet, so
   every declaration is kept as its tokens */
node_index parser::declaration()
{
    return unparsed_declaration();
}

} // namespace cplusplus
