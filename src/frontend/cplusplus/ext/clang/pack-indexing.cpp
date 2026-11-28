/*
 * pack-indexing.cpp: [ext:clang_pack_indexing] (clang Language Extensions,
 * Pack indexing: C++26's P2662R3, which clang accepts in every mode and
 * libc++'s tuple and integer_sequence use): one element of a pack, from 0.
 *
 *     simple-type-specifier:
 *         clang-pack-index-specifier
 *
 *     clang-pack-index-specifier:
 *         identifier ... [ constant-expression ]
 *
 *     primary-expression:
 *         clang-pack-index-expression
 *
 *     clang-pack-index-expression:
 *         identifier ... [ constant-expression ]
 *
 * The identifier names a pack; the index is a converted constant expression
 * of type std::size_t less than the pack's size.  The element is what the
 * identifier denotes in the pattern of an expansion of the pack
 * ([temp.variadic]/5) analysed for that element.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* identifier ... [ and not an attribute-specifier's [[ ([dcl.attr.grammar]
   /7), which may follow a declarator's ellipsis */
bool parser::pack_index_follows()
{
    return at_identifier() && at(preprocessing_op_or_punc::ELLIPSIS, 1) &&
           at(preprocessing_op_or_punc::LEFT_BRACKET, 2) &&
           !at(preprocessing_op_or_punc::LEFT_BRACKET, 3);
}

node_index parser::clang_pack_index(syntax_kind kind)
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* identifier */
    add_token(); /* ... */
    add_token(); /* [ */
    flag_set greater(greater_is_operator, true);
    constant_expression();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_BRACKET))
        return expected("']' to end the pack index", "ext.clang");
    return finish_node(kind, mark, first);
}

/*
 * The element n names.  dependent: the pack is the template's own, not
 * bound to elements, or the index is value-dependent
 * ([temp.dep.constexpr]/2), and element is not set.
 */
analysis_result semantic_analysis::pack_index(SyntaxRef n, uint32_t &element,
                                              bool &dependent)
{
    element = 0;
    dependent = false;
    token_index name = n[0].token();
    uint32_t count = 0;
    bool own = false;
    if (!expansion_count(name, name + 1, count, &own)) {
        if (!own)
            return not_supported_because(
                n, "ext.clang", "an index into a pack that has no rules here");
        dependent = true;
        return analysis_result::well_formed;
    }
    SyntaxElement index = n[3];
    token_index first = first_token(index);
    token_index end = first + 1;
    if (!index.is_token())
        end = index.end_token();
    if (names_dependent_entity(first, end)) {
        dependent = true;
        return analysis_result::well_formed;
    }
    expression_meaning m;
    analysis_result r = expression(index, m);
    if (r == analysis_result::well_formed)
        VTRY_SET(r, operand_value(n, m));
    bool constant = false;
    if (is_integral(m.type)) {
        VTRY_SET(r, converted_constant_expression(
                        n, m, fundamental_type::unsigned_long_int, constant));
    }
    if (!constant)
        return ill_formed(n,
                          "the index of a pack indexing is an integral "
                          "constant expression",
                          "ext.clang");
    if (m.value >= count)
        return ill_formed(n,
                          "the index of a pack indexing is less than the "
                          "size of the pack",
                          "ext.clang");
    element = static_cast<uint32_t>(m.value);
    return analysis_result::well_formed;
}

/* [ext:clang_pack_indexing] T...[I] as a type */
analysis_result semantic_analysis::pack_index_type(SyntaxRef n,
                                                   qualified_type &type,
                                                   uint32_t &cv)
{
    uint32_t element = 0;
    bool dependent = false;
    VTRY(r, pack_index(n, element, dependent));
    if (dependent) {
        /* [temp.dep.type]: a type known by how it is written */
        type = qualified_type::make(dependent_written_type(n), cv_none);
        return analysis_result::well_formed;
    }
    token_index name = n[0].token();
    expansion_scope scope(*this, true, element, true);
    lookup_result found =
        lookup_unqualified(tokens.identifier(name), lookup_filter::types);
    if (!found.entity ||
        entity_table[found.entity].kind != entity_kind::typedef_name)
        return not_supported_because(
            n, "ext.clang",
            "a pack-index-specifier whose element is not an analysed type");
    qualified_type t = entity_table[found.entity].type;
    type = qualified_type::make(t.unqualified(), cv_none);
    cv |= t.cv();
    return analysis_result::well_formed;
}

/* [ext:clang_pack_indexing] E...[I] as an expression */
analysis_result semantic_analysis::pack_index_expression(SyntaxRef n,
                                                         expression_meaning &m)
{
    uint32_t element = 0;
    bool dependent = false;
    VTRY(r, pack_index(n, element, dependent));
    if (dependent)
        return not_supported_because(n, "temp.dep.expr",
                                     "a type-dependent pack indexing");
    expansion_scope scope(*this, true, element);
    lookup_result found =
        lookup_unqualified(tokens.identifier(n[0].token()), lookup_filter::any);
    if (found.entity &&
        entity_table[found.entity].kind == entity_kind::typedef_name)
        return not_supported_because(
            n, "expr.type.conv",
            "a pack-index-specifier as the type of a conversion");
    return expression(n[0], m);
}

} // namespace cplusplus
