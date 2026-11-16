/*
 * The kinds of syntax tree node: the grammar's nonterminals, by the name
 * the standard gives them and the subclause whose grammar defines them
 * (docs/cxx-spec/grammar-index.md).  A production that only passes its
 * operand on makes no node (doc/PLAN-cxx-parse.md).
 *
 * SYNTAX_NODE(name, nonterminal, subclause)
 */
SYNTAX_NODE(translation_unit, "translation-unit", "basic.link")
SYNTAX_NODE(declaration_seq, "declaration-seq", "dcl.pre")

/*
 * Scaffolding, not grammar: both are counted, and both are at zero on
 * well-formed input when the parser is complete.
 *   unparsed   a balanced token sequence of a part of the grammar that is
 *              not written yet
 *   erroneous  what was skipped after a syntax error
 * Their tokens are the node's token range; they have no children.
 */
SYNTAX_NODE(unparsed, "(unparsed)", "")
SYNTAX_NODE(erroneous, "(erroneous)", "")
