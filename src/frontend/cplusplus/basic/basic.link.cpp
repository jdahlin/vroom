/*
 * basic.link.cpp: [basic.link] Program and linkage: the translation unit.
 *
 *     translation-unit:
 *         declaration-seq_opt
 *         global-module-fragment_opt module-declaration declaration-seq_opt private-module-fragment_opt
 *
 * [basic.link]/1: "A translation unit consists of a sequence of
 * declarations."
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::translation_unit()
{
    size_t mark = begin_node();
    token_index first = position;
    /* the second alternative ([module.unit]) is not written yet: its
       module-declaration is one of the unparsed declarations for now */
    declaration_seq(false);
    tree.root = finish_node(syntax_kind::translation_unit, mark, first);
    pending.resize(mark);
    return tree.root;
}

} // namespace cplusplus
