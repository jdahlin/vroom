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
    /* the second alternative: a module unit ([module.unit]), from C++20 */
    if (tokens.files.behavior.has(language_feature::modules) &&
        (at_module_declaration() ||
         (at_phase4_keyword(preprocessing_token_kind::module_keyword) &&
          at(preprocessing_op_or_punc::SEMICOLON, 1))))
        module_unit_rest(!at_module_declaration());
    else
        declaration_seq(false);
    tree.root = finish_node(syntax_kind::translation_unit, mark, first);
    pending.resize(mark);
    return tree.root;
}

} // namespace cplusplus
