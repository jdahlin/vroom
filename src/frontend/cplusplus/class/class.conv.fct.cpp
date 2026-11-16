/*
 * class.conv.fct.cpp: [class.conv.fct] Conversion functions.
 *
 *     conversion-function-id:
 *         operator conversion-type-id
 *
 *     conversion-type-id:
 *         type-specifier-seq conversion-declarator_opt
 *
 *     conversion-declarator:
 *         ptr-operator conversion-declarator_opt
 *
 * [class.conv.fct]/3: "The conversion-type-id shall not represent a
 * function type nor an array type.  The conversion-type-id in a
 * conversion-function-id is the longest sequence of tokens that could
 * possibly form a conversion-type-id."  So every ptr-operator after the
 * type-specifier-seq is the conversion-declarator's (operator int*() is
 * operator (int*) ()), and a '(' or '[' after it is not.  The
 * conversion-function-id itself is made by over.oper.cpp.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::conversion_type_id()
{
    size_t mark = begin_node();
    token_index first = position;
    decl_specifier_facts specifiers;
    specifiers.type_specifier_seq = true;
    /* CWG2413: a conversion-type-id is a type-id-only context (C++20,
       with [temp.res]/5) */
    specifiers.implicit_typename = tokens.files.behavior.has(language_feature::implicit_typename);
    decl_specifier_seq(specifiers, true);
    if (failed())
        return 0;
    if (!specifiers.node)
        return expected("a type-specifier-seq after operator",
                        "class.conv.fct");
    size_t declarator_mark = begin_node();
    unsigned operators = 0;
    while (at_ptr_operator()) {
        /* before C++11 (N2118) && is no ptr-operator: in
           &C::operator int && x it is the logical and */
        if (at(preprocessing_op_or_punc::AMPERSAND_AMPERSAND) &&
            !tokens.files.behavior.has(language_feature::rvalue_references))
            break;
        derivation kind = derivation::none;
        ptr_operator(&kind);
        if (failed())
            return 0;
        operators++;
    }
    /* conversion-declarator: ptr-operator conversion-declarator_opt, so
       each ptr-operator but the last has the rest as its
       conversion-declarator; one ptr-operator alone passes through */
    for (unsigned i = operators; i-- > 1;) {
        size_t from = declarator_mark + i - 1;
        finish_node(syntax_kind::conversion_declarator, from,
                    tree[pending[from].index()].first_token);
    }
    if (!operators && pending.size() == mark + 1)
        return 0; /* a type-specifier-seq alone passes through */
    return finish_node(syntax_kind::conversion_type_id, mark, first);
}

} // namespace cplusplus
