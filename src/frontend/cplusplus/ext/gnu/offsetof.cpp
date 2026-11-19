/*
 * offsetof.cpp: [ext:gnu_offsetof] (GCC manual 6.53, Support for
 * offsetof): the primitive <cstddef>'s offsetof expands to
 * ([support.types.layout]/1: "The macro offsetof(type, member-designator)
 * ... accepts a restricted set of type arguments"; libc++ applies it to
 * classes that are not standard-layout too, which GCC and clang allow).
 *
 *     primary-expression:
 *         gnu-offsetof-expression
 *
 *     gnu-offsetof-expression:
 *         __builtin_offsetof ( type-id , gnu-offsetof-member-designator )
 *
 *     gnu-offsetof-member-designator:
 *         identifier
 *         gnu-offsetof-member-designator . identifier
 *         gnu-offsetof-member-designator [ expression ]
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::gnu_offsetof_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* __builtin_offsetof */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after __builtin_offsetof", "ext.gnu");
    flag_set greater(greater_is_operator, true);
    type_id();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::COMMA))
        return expected("',' after the type-id", "ext.gnu");
    if (!at_identifier())
        return expected("a member's name", "ext.gnu");
    add_token();
    for (;;) {
        if (accept(preprocessing_op_or_punc::DOT)) {
            if (!at_identifier())
                return expected("a member's name after '.'", "ext.gnu");
            add_token();
        } else if (accept(preprocessing_op_or_punc::LEFT_BRACKET)) {
            expression();
            if (failed())
                return 0;
            if (!accept(preprocessing_op_or_punc::RIGHT_BRACKET))
                return expected("']' to end the subscript", "ext.gnu");
        } else {
            break;
        }
    }
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to end __builtin_offsetof", "ext.gnu");
    return finish_node(syntax_kind::gnu_offsetof_expression, mark, first);
}

/* [ext:gnu_offsetof] a prvalue of type std::size_t, a constant: the
   offset in bytes of the designated subobject from the start of an
   object of the type.  A member found in a base class or through an
   anonymous union is not handled yet; a template's own operand is
   dependent, analysed when it is instantiated */
analysis_result semantic_analysis::gnu_offsetof(node_index n,
                                                expression_meaning &m)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    qualified_type t;
    analysis_result r = type_id(children[2].index(), t);
    if (r != analysis_result::well_formed)
        return r;
    if (depends_on_template_parameter(t))
        return not_supported(n, "temp.dep.constexpr");
    const implementation_defined_behavior &b = tokens.files.behavior;
    uint64_t offset = 0;
    /* children[4 .. child_count - 1): the member-designator */
    for (uint32_t i = 4; i + 1 < node.child_count; i++) {
        syntax_child c = children[i];
        if (syntax(c).is(preprocessing_op_or_punc::DOT))
            continue;
        const struct type record = type_table[t.unqualified()];
        if (syntax(c).is(preprocessing_op_or_punc::LEFT_BRACKET)) {
            if (record.kind != type_kind::array)
                return ill_formed(n,
                                  "a subscript in the member-designator "
                                  "shall designate an array element",
                                  "ext.gnu");
            expression_meaning e;
            r = expression(children[++i], e);
            if (r == analysis_result::well_formed)
                r = operand_value(n, e);
            bool constant = false;
            if (r == analysis_result::well_formed && is_integral(e.type))
                r = integral_constant_expression(n, e, constant);
            if (r != analysis_result::well_formed)
                return r;
            if (!constant)
                return not_supported(n, "ext.gnu");
            i++; /* ] */
            t = record.operand;
            offset += static_cast<uint64_t>(
                          integer_value(b, fundamental_of(e.type), e.value)) *
                      size_of_type(t);
            continue;
        }
        if (record.kind != type_kind::class_type ||
            !entity_table[record.entity].size)
            return ill_formed(n, "offsetof requires a complete class type",
                              "ext.gnu");
        entity_index member = declared_in(
            record.entity, tokens.identifier(c.index()), lookup_filter::any);
        if (!member)
            return not_supported(n, "ext.gnu");
        const entity &e = entity_table[member];
        if (e.kind != entity_kind::variable || !e.nonstatic_member ||
            e.anonymous_object)
            return not_supported(n, "ext.gnu");
        offset += e.offset;
        t = e.type;
    }
    m = integer_constant(fundamental_type::unsigned_long_int, offset,
                         node.first_token);
    return analysis_result::well_formed;
}

} // namespace cplusplus
