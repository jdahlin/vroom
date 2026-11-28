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

/* the member `name` of class c or of its base classes
   ([class.member.lookup]): added to found, its subobject's offset in c to
   offset.  False for a virtual base, whose offset is not one of the class */
bool semantic_analysis::offsetof_member(entity_index c, uint32_t name,
                                        uint64_t at,
                                        owned_array<entity_index> &found,
                                        uint64_t &offset)
{
    entity_index member = declared_in(c, name, lookup_filter::any);
    if (member) {
        found.append(member);
        offset = at;
        return true;
    }
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.details().base_count; i++) {
        const entity &b = entity_table[class_bases[x.details().first_base + i]];
        const struct type &t = type_table[b.type.unqualified()];
        if (b.virtual_base || t.kind != type_kind::class_type ||
            !offsetof_member(t.entity, name, at + b.details().offset, found,
                             offset))
            return false;
    }
    return true;
}

/* [ext:gnu_offsetof] a prvalue of type std::size_t, a constant: the
   offset in bytes of the designated subobject from the start of an
   object of the type.  A member of a virtual base, or one found in more
   than one base subobject, is not handled yet; a template's own operand is
   dependent, analysed when it is instantiated */
analysis_result semantic_analysis::gnu_offsetof(SyntaxRef n,
                                                expression_meaning &m)
{
    qualified_type t;
    VTRY(r, type_id(n[2], t));
    if (depends_on_template_parameter(t))
        return not_supported_because(n, "temp.dep.constexpr",
                                     "offsetof in a dependent type");
    const implementation_defined_behavior &b = tokens.files.behavior;
    uint64_t offset = 0;
    /* node[4 .. child_count - 1): the member-designator */
    for (uint32_t i = 4; i + 1 < n.size(); i++) {
        SyntaxElement c = n[i];
        if (c.is(preprocessing_op_or_punc::DOT))
            continue;
        const struct type record = type_table[t.unqualified()];
        if (c.is(preprocessing_op_or_punc::LEFT_BRACKET)) {
            if (record.kind != type_kind::array)
                return ill_formed(n,
                                  "a subscript in the member-designator "
                                  "shall designate an array element",
                                  "ext.gnu");
            expression_meaning e;
            r = expression(n[++i], e);
            if (r == analysis_result::well_formed)
                r = operand_value(n, e);
            bool constant = false;
            if (r == analysis_result::well_formed && is_integral(e.type))
                VTRY_SET(r, integral_constant_expression(n, e, constant));
            if (!constant)
                return not_supported_because(
                    n, "ext.gnu",
                    "offsetof with an array index that is not an integral "
                    "constant");
            i++; /* ] */
            t = record.operand;
            offset += static_cast<uint64_t>(
                          integer_value(b, fundamental_of(e.type), e.value)) *
                      size_of_type(t);
            continue;
        }
        require_complete(record.entity);
        if (record.kind != type_kind::class_type ||
            !entity_table[record.entity].details().size)
            return ill_formed(n, "offsetof requires a complete class type",
                              "ext.gnu");
        owned_array<entity_index> found;
        uint64_t base_offset = 0;
        if (!offsetof_member(record.entity, tokens.identifier(c.token()), 0,
                             found, base_offset) ||
            found.size() != 1)
            return not_supported_because(
                n, "ext.gnu",
                "offsetof of a member name not found once in the class and "
                "its non-virtual bases");
        entity_index member = found[0];
        offset += base_offset;
        const entity &e = entity_table[member];
        if (e.kind != entity_kind::variable || !e.nonstatic_member ||
            e.details().anonymous_object)
            return not_supported_because(n, "ext.gnu",
                                         "offsetof of a name that is not a "
                                         "plain non-static data member");
        offset += e.details().offset;
        /* [class.union.anon]/1: a member of an anonymous union is one of the
           class whose member its unnamed object is, at that object's offset
           (and so on, outwards) */
        entity_index member_of = e.enclosing;
        while (entity_table[member_of].details().anonymous_object) {
            const entity &object = entity_table
                [entity_table[member_of].details().anonymous_object];
            offset += object.details().offset;
            member_of = object.enclosing;
        }
        t = e.type;
    }
    m = integer_constant(fundamental_type::unsigned_long_int, offset,
                         n.first_token());
    return analysis_result::well_formed;
}

} // namespace cplusplus
