/*
 * bit-int.cpp: [ext:clang_bit_int] (clang Language Extensions, _BitInt in
 * C++): C23's bit-precise signed integer type, which libc++'s
 * __math/abs.h overloads abs for.
 *
 *     simple-type-specifier:
 *         clang-bit-int-type-specifier
 *
 *     clang-bit-int-type-specifier:
 *         _BitInt ( constant-expression )
 *
 * The type has clang's layout on AArch64 (bit_int_layout).  Its values
 * (conversions, promotions, arithmetic) are not analysed yet: an
 * expression of the type is unsupported, and so is unsigned _BitInt(N).
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::clang_bit_int_type_specifier()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* _BitInt */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after _BitInt", "ext.clang");
    flag_set greater(greater_is_operator, true);
    constant_expression();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to end the _BitInt type specifier", "ext.clang");
    return finish_node(syntax_kind::clang_bit_int_type_specifier, mark, first);
}

/* [ext:clang_bit_int] _BitInt(N): C23 6.7.3.1/2 "The parenthesized
   constant expression that follows the _BitInt keyword shall be an integer
   constant expression N that specifies the width ... The value of N for
   signed _BitInt shall be greater than or equal to 2", and no more than
   BITINT_MAXWIDTH.  In a template's own declaration N may be a non-type
   template-parameter ([temp.dep.constexpr]/2), as for an array's bound */
analysis_result semantic_analysis::clang_bit_int_type(SyntaxRef n,
                                                      bool is_unsigned,
                                                      qualified_type &type)
{
    SyntaxElement width = n[2];
    if (entity_index named = value_parameter_named(width)) {
        if (!is_integral(entity_table[named].type))
            return not_supported(n, "temp.dep.constexpr");
        type = qualified_type::make(type_table.bit_int(0, named, is_unsigned),
                                    cv_none);
        return analysis_result::well_formed;
    }
    expression_meaning m;
    analysis_result r = expression(width, m);
    if (r == analysis_result::well_formed)
        r = operand_value(n, m);
    bool constant = false;
    if (r == analysis_result::well_formed && is_integral(m.type))
        VTRY_SET(r, integral_constant_expression(n, m, constant));
    if (!constant)
        return not_supported(n, "ext.clang");
    __int128 bits =
        integer_value(tokens.files.behavior, fundamental_of(m.type), m.value);
    /* (an unsigned _BitInt may be one bit wide) */
    __int128 least = 2;
    if (is_unsigned)
        least = 1;
    if (bits < least || bits > tokens.files.behavior.bit_precise_maximum_width)
        return ill_formed(n,
                          "the width of a signed _BitInt shall be at least 2 "
                          "and at most BITINT_MAXWIDTH",
                          "ext.clang");
    type = qualified_type::make(
        type_table.bit_int(static_cast<uint64_t>(bits), 0, is_unsigned),
        cv_none);
    return analysis_result::well_formed;
}

/* [impl.clang] _BitInt(N)'s size, or alignment, on AArch64 (AAPCS64's
   rules for it): up to 64 bits, the bytes rounded up to a power of two and
   aligned to it; wider, a multiple of 16 bytes aligned to 16 */
uint64_t semantic_analysis::bit_int_layout(uint64_t width, bool alignment) const
{
    if (!width)
        return 0;
    uint64_t bytes = (width + 7) / 8;
    if (width > 64) {
        if (alignment)
            return 16;
        return (bytes + 15) / 16 * 16;
    }
    return power_of_two_at_least(bytes);
}

} // namespace cplusplus
