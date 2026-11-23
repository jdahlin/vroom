/*
 * bit-field.cpp: [class.bit] Bit-fields: the declaration of a bit-field
 * member (/1, /2); its allocation is the layout's
 * (abi/itanium/layout.cpp).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [class.bit]/1: "A member-declarator of the form
 *     identifier_opt attribute-specifier-seq_opt : constant-expression brace-or-equal-initializer_opt
 * specifies a bit-field.  The optional attribute-specifier-seq appertains
 * to the entity being declared.  A bit-field shall not be a static member.
 * A bit-field shall have integral or enumeration type ...  The
 * constant-expression shall be an integral constant expression with a
 * value greater than or equal to zero and is called the width of the
 * bit-field.  If the width of a bit-field is larger than the width of the
 * bit-field's type (or, in case of an enumeration type, of its underlying
 * type), the extra bits are padding bits.  Allocation of bit-fields within
 * a class object is implementation-defined."  /2: "A declaration for a
 * bit-field that omits the identifier declares an unnamed bit-field.
 * Unnamed bit-fields are not members and cannot be initialized. ... As a
 * special case, an unnamed bit-field with a width of zero specifies
 * alignment of the next bit-field at an allocation unit boundary.  Only
 * when declaring an unnamed bit-field may the width be zero."
 *
 * The allocation is the layout's, at the closing } (abi/itanium/layout.cpp:
 * clang's for aarch64 Linux).  Bit-fields with
 * padding bits, with an initializer or attributes are not
 * analysed yet.
 */
analysis_result semantic_analysis::member_bit_field(entity_index c,
                                                    SyntaxRef declaration,
                                                    SyntaxElement d,
                                                    const specified &s,
                                                    uint8_t access)
{
    token_index id = 0;
    uint32_t k = 0;
    if (k < d.size() && d[k].is_token() && d[k].is_identifier())
        id = d[k++].token();
    if (k + 2 != d.size() || !d[k].is(preprocessing_op_or_punc::COLON))
        /* attributes, a brace-or-equal-initializer */
        return not_supported(d.index(), "class.bit");
    SyntaxElement width_expression = d[k + 1];
    if (!s.has_type || s.typedef_ || s.static_ || s.extern_ || s.mutable_ ||
        s.inline_ || s.constexpr_ || s.aligned || s.gnu_flags)
        return not_supported(declaration, "class.bit");
    qualified_type type = s.type;
    const struct type record = type_table[type.unqualified()];
    /* [class.bit]/1: "A bit-field shall have integral or enumeration
       type"; an enumeration whose underlying type is not fixed yet (an
       incomplete one) has no size */
    bool enumeration = record.kind == type_kind::enumeration;
    if (enumeration && !size_of_type(type))
        return not_supported(declaration, "class.bit");
    if (!enumeration && (record.kind != type_kind::fundamental ||
                         !fundamental_type_is_integral(record.fundamental)))
        return ill_formed(declaration,
                          "a bit-field shall have integral or enumeration "
                          "type",
                          "class.bit");
    expression_meaning w;
    VTRY(r, expression(width_expression, w));
    VTRY_SET(r, operand_value(declaration, w));
    promote_enumeration(w);
    if (!is_integral(w.type))
        return not_supported(declaration, "class.bit");
    /* [class.bit]/1: "The constant-expression shall be an integral
       constant expression with a value greater than or equal to zero and
       is called the width of the bit-field." */
    bool constant_width = false;
    VTRY_SET(r, integral_constant_expression(declaration, w, constant_width));
    if (!constant_width)
        return not_supported(declaration, "class.bit");
    __int128 width_value =
        integer_value(tokens.files.behavior, fundamental_of(w.type), w.value);
    if (width_value < 0)
        return ill_formed(declaration,
                          "the width of a bit-field shall be greater than or "
                          "equal to zero",
                          "class.bit");
    if (width_value == 0 && id)
        return ill_formed(declaration,
                          "only when declaring an unnamed bit-field may the "
                          "width be zero",
                          "class.bit");
    uint64_t unit = size_of_type(type) * 8;
    uint64_t alignment = alignment_of_type(type);
    if (static_cast<unsigned __int128>(width_value) > unit || !alignment)
        /* padding bits (clang's warning) */
        return not_supported(declaration, "class.bit");
    uint64_t width = static_cast<uint64_t>(width_value);
    uint32_t name = 0;
    if (id) {
        name = tokens.identifier(id);
        if (entity_table.find(c, name))
            return not_supported(declaration, "class.mem");
    }
    entity_index e = entity_table.make(entity_kind::variable, name, c);
    entity_table[e].type = type;
    entity_table[e].nonstatic_member = true;
    entity_table[e].defined = true;
    entity_table[e].field = entity_table[c].member_count;
    entity_table[e].bit_field = true;
    entity_table[e].bit_width = static_cast<uint16_t>(width);
    entity_table[e].linkage_of_name = entity_table[c].linkage_of_name;
    entity_table[e].access = access;
    class_members.append(e);
    entity_table[c].member_count++;
    entity_table.add_declaration(e, declaration);
    if (name)
        entity_table.bind(c, name, e);
    last_declared = e;
    return analysis_result::well_formed;
}

} // namespace cplusplus
