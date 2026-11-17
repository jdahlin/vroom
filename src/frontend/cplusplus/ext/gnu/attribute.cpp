/*
 * attribute.cpp: [ext:gnu_attributes] GCC's attribute syntax (GCC manual
 * 6.39, Attribute Syntax), which clang accepts as GCC does:
 *
 *     gnu-attribute-specifier:
 *         __attribute__ ( ( gnu-attribute-list ) )
 *
 *     gnu-attribute-specifier-seq:
 *         gnu-attribute-specifier-seq_opt gnu-attribute-specifier
 *
 *     gnu-attribute-list:
 *         gnu-attribute_opt
 *         gnu-attribute-list , gnu-attribute_opt
 *
 *     gnu-attribute:
 *         identifier
 *         identifier ( gnu-attribute-arguments_opt )
 *
 *     gnu-attribute-arguments:
 *         identifier
 *         identifier , expression-list
 *         expression-list
 *
 * An attribute's identifier may be a keyword, as an attribute-token's
 * ([dcl.attr.grammar]/5).  Where it goes:
 *
 *     attribute-specifier:
 *         gnu-attribute-specifier
 *
 *     decl-specifier:
 *         gnu-attribute-specifier
 *
 *     declarator:
 *         ptr-declarator gnu-attribute-specifier-seq
 *         noptr-declarator parameters-and-qualifiers trailing-return-type gnu-attribute-specifier-seq
 *
 *     named-namespace-definition:
 *         inline_opt namespace attribute-specifier-seq_opt identifier gnu-attribute-specifier-seq { namespace-body }
 *
 *     unnamed-namespace-definition:
 *         inline_opt namespace attribute-specifier-seq_opt gnu-attribute-specifier-seq { namespace-body }
 *
 * "An attribute specifier list may appear as part of a struct, union or
 * enum specifier", "immediately before a declarator", "immediately before
 * the comma, = or semicolon terminating the declaration of an identifier
 * other than a function definition", and among the declaration specifiers.
 * The places the standard has for an attribute-specifier-seq take this
 * one too (at_attribute_specifier); the others are entered from their
 * production: gnu_attributes_after_declarator, decl_specifier_seq,
 * namespace_definition.
 *
 * ("The parameters ... can be: an identifier ... ; an identifier followed
 * by a comma and a non-empty comma-separated list of expressions ... ; a
 * possibly empty comma-separated list of expressions.")  What an attribute
 * means, and which of them it takes, is not grammar.
 */
#include "../../cplusplus-internal.h"

namespace cplusplus
{

bool parser::at_gnu_attribute(unsigned ahead)
{
    return at(extension_keyword::attribute, ahead);
}

node_index parser::gnu_attribute_specifier()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* __attribute__ */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
        !accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'((' after __attribute__", "ext.gnu");
    size_t list_mark = begin_node();
    token_index list_first = position;
    unsigned count = 0;
    for (;;) {
        if (at_attribute_identifier()) {
            size_t attribute_mark = begin_node();
            token_index attribute_first = position;
            add_token();
            if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
                add_token();
                flag_set greater(greater_is_operator, true);
                bool more = !at(preprocessing_op_or_punc::RIGHT_PARENTHESIS);
                if (at_attribute_identifier() &&
                    (at(preprocessing_op_or_punc::COMMA, 1) ||
                     at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, 1))) {
                    add_token();
                    more = accept(preprocessing_op_or_punc::COMMA);
                }
                if (more) {
                    expression_list();
                    if (failed())
                        return 0;
                }
                if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                    return expected("')' to end the attribute's parameters",
                                    "ext.gnu");
                finish_node(syntax_kind::gnu_attribute, attribute_mark,
                            attribute_first);
            }
        } else if (!at(preprocessing_op_or_punc::COMMA) &&
                   !at(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
            return expected("an attribute", "ext.gnu");
        }
        count++;
        if (!accept(preprocessing_op_or_punc::COMMA))
            break;
    }
    if (count > 1)
        finish_node(syntax_kind::gnu_attribute_list, list_mark, list_first);
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
        !accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("'))' to end the attribute-specifier", "ext.gnu");
    return finish_node(syntax_kind::gnu_attribute_specifier, mark, first);
}

/* attribute-specifier ... in the places only GCC's syntax has: 0 when
   there is none */
node_index parser::gnu_attributes_opt()
{
    node_index last = 0;
    while (at_gnu_attribute()) {
        last = gnu_attribute_specifier();
        if (failed())
            return 0;
    }
    return last;
}

VPRIV bool has_gnu_attribute(const syntax_tree &tree, node_index n)
{
    if (tree[n].kind == syntax_kind::gnu_attribute_specifier)
        return true;
    if (tree[n].kind != syntax_kind::attribute_specifier_seq)
        return false;
    for (uint32_t i = 0; i < tree[n].child_count; i++) {
        syntax_child c = tree.children(n)[i];
        if (!c.is_token() &&
            tree[c.index()].kind == syntax_kind::gnu_attribute_specifier)
            return true;
    }
    return false;
}

VPRIV bool only_gnu_attributes(const syntax_tree &tree, node_index n)
{
    if (tree[n].kind == syntax_kind::gnu_attribute_specifier)
        return true;
    if (tree[n].kind != syntax_kind::attribute_specifier_seq)
        return false;
    for (uint32_t i = 0; i < tree[n].child_count; i++) {
        syntax_child c = tree.children(n)[i];
        if (c.is_token() ||
            tree[c.index()].kind != syntax_kind::gnu_attribute_specifier)
            return false;
    }
    return true;
}

/* ---- semantic analysis ---------------------------------------------- */

/*
 * The attributes of a gnu-attribute-specifier after a declarator-id.  Only
 * one has rules yet, GCC manual 6.34.1 Common Variable Attributes:
 * "aligned (alignment): The aligned attribute specifies a minimum
 * alignment for the variable or structure field, measured in bytes. When
 * specified, alignment must be an integer constant power of 2.  Specifying
 * no alignment argument implies the ideal alignment for the target." (the
 * target's largest, 16 on aarch64 and x86-64, as clang's).  The others are
 * unsupported with ext.gnu.
 */
analysis_result
semantic_analysis::gnu_attribute_specifier(node_index n, declarator_meaning &m)
{
    owned_array<node_index> attributes;
    owned_array<node_index> work;
    work.append(n);
    while (!work.empty()) {
        node_index next = work.back();
        work.resize(work.size() - 1);
        for (uint32_t i = 0; i < tree[next].child_count; i++) {
            syntax_child c = tree.children(next)[i];
            if (syntax_is(tree, c, syntax_kind::gnu_attribute))
                attributes.append(c.index());
            else if (syntax_is(tree, c, syntax_kind::gnu_attribute_list))
                work.append(c.index());
            else if (!c.is_token())
                return not_supported(n, "ext.gnu");
        }
    }
    for (node_index a : attributes) {
        const syntax_node &node = tree[a];
        const syntax_child *children = tree.children(a);
        if (!node.child_count || !children[0].is_token())
            return not_supported(a, "ext.gnu");
        owned_buffer spelling;
        tokens.spelling(children[0].index(), spelling.get());
        bool aligned =
            (spelling.size() == 7 && !memcmp(spelling.data(), "aligned", 7)) ||
            (spelling.size() == 11 &&
             !memcmp(spelling.data(), "__aligned__", 11));
        if (!aligned)
            return not_supported(a, "ext.gnu");
        uint64_t alignment = 16;
        if (node.child_count == 4) {
            expression_meaning e;
            analysis_result r = expression(children[2], e);
            if (r != analysis_result::well_formed)
                return r;
            if (!is_integral(e.type) || e.category != value_category::prvalue ||
                e.evaluation != constness::constant)
                return not_supported(a, "ext.gnu");
            alignment = e.value;
            if (!alignment || (alignment & (alignment - 1)) ||
                alignment > (1u << 28))
                /* clang: "requested alignment is not a power of 2" */
                return not_supported(a, "ext.gnu");
        } else if (node.child_count != 1 && node.child_count != 3) {
            return not_supported(a, "ext.gnu");
        }
        if (alignment > m.aligned)
            m.aligned = alignment;
    }
    return analysis_result::well_formed;
}

} // namespace cplusplus
