/*
 * dcl.struct.bind.cpp: [dcl.struct.bind] Structured binding declarations,
 * the third alternative of [dcl.pre]/1's
 *
 *     simple-declaration:
 *         attribute-specifier-seq_opt decl-specifier-seq ref-qualifier_opt [ identifier-list ] initializer ;
 *
 * and of [stmt.iter]'s
 *
 *     for-range-declaration:
 *         attribute-specifier-seq_opt decl-specifier-seq ref-qualifier_opt [ identifier-list ]
 *
 * with [cpp.pre]'s
 *
 *     identifier-list:
 *         identifier
 *         identifier-list , identifier
 *
 * [dcl.struct.bind]/1: "A structured binding declaration introduces the
 * identifiers v0, v1, v2, ... of the identifier-list as names
 * ([basic.scope.declarative]) of structured bindings."  Which
 * decl-specifiers it may have ([dcl.struct.bind]/1: "The decl-specifier-seq
 * shall contain only the type-specifier auto and cv-qualifiers") is a
 * semantic rule.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/* ref-qualifier_opt [ after a decl-specifier-seq: no declarator starts
   with [ ([dcl.attr.grammar]/7: [[ is an attribute-specifier's) */
bool parser::structured_binding_follows()
{
    unsigned i = 0;
    if (at(preprocessing_op_or_punc::AMPERSAND) ||
        at(preprocessing_op_or_punc::AMPERSAND_AMPERSAND))
        i = 1;
    return at(preprocessing_op_or_punc::LEFT_BRACKET, i) &&
           !at(preprocessing_op_or_punc::LEFT_BRACKET, i + 1);
}

/*
 * ref-qualifier_opt [ identifier-list ]: the tokens are added to the
 * caller's node, the identifier-list is a node when it has more than one
 * identifier.  Structured bindings are C++17's (P0217R3); in an earlier
 * edition they are clang's extension, P9's.
 */
node_index parser::structured_binding_identifiers()
{
    if (!tokens.files.behavior.has(language_feature::structured_bindings))
        return beyond_this_step();
    if (!accept(preprocessing_op_or_punc::AMPERSAND))
        accept(preprocessing_op_or_punc::AMPERSAND_AMPERSAND);
    add_token(); /* [ */
    size_t mark = begin_node();
    token_index first = position;
    unsigned count = 0;
    /* [basic.scope.pdecl]/10: "The point of declaration of a structured
       binding is immediately after the identifier-list of the structured
       binding declaration." */
    for (;;) {
        if (!at_identifier())
            return expected("an identifier", "dcl.struct.bind");
        names.declare(tokens.identifier(position), name_kind::other);
        add_token();
        count++;
        if (!accept(preprocessing_op_or_punc::COMMA))
            break;
    }
    if (count > 1)
        finish_node(syntax_kind::identifier_list, mark, first);
    if (!accept(preprocessing_op_or_punc::RIGHT_BRACKET))
        return expected("']' to end the identifier-list", "dcl.struct.bind");
    return 0;
}

} // namespace cplusplus
