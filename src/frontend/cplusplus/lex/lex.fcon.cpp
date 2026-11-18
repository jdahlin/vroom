/*
 * lex.fcon.cpp: [lex.fcon] Floating-point literals: the type and value of a
 * pp-number that lex.ppnumber.cpp read as a floating-point-literal.
 *
 *     floating-point-literal:
 *         decimal-floating-point-literal
 *         hexadecimal-floating-point-literal
 */
#include "common/softfloat.h"

#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

/* a floating-point type's format ([basic.fundamental]/12): fraction bits,
   the largest biased exponent, the bias; and the extreme values clang names
   in its range diagnostics */
struct floating_point_format {
    int fraction_bits, maximum_exponent, bias;
    const char *maximum, *minimum;
};

floating_point_format format_of(const implementation_defined_behavior &b,
                                fundamental_type type)
{
    switch (type) {
    case fundamental_type::float16:
        return {10, 0x1F, 15, "6.5504E+4", "5.9604645E-8"};
    case fundamental_type::float_:
        return {23, 0xFF, 127, "3.40282347E+38", "1.40129846E-45"};
    case fundamental_type::long_double:
        if (b.long_double_fraction_bits == 112)
            return {112, 0x7FFF, 16383,
                    "1.18973149535723176508575932662800702E+4932",
                    "6.47517511943802511092443895822764655E-4966"};
        [[fallthrough]];
    default:
        return {52, 0x7FF, 1023, "1.7976931348623157E+308",
                "4.9406564584124654E-324"};
    }
}

/*
 * The scaled value rounded once, to nearest with ties to even, into a format.
 * The parser rounds to a number of fraction bits within binary128's exponent
 * range; a value below the format's smallest normal has fewer bits in the
 * format (a subnormal), so it is parsed again at exactly that many, then
 * moved into the format, which no longer rounds.
 */
unsigned __int128 round_to_format(const char *s, size_t n,
                                  const floating_point_format &f)
{
    unsigned __int128 wide =
        vsoftfloat_parse_with_precision(s, n, f.fraction_bits);
    if (f.fraction_bits == 112)
        return wide;
    int exponent = (int)((wide >> 112) & 0x7FFF) - 16383; /* unbiased */
    int minimum_exponent = 1 - f.bias;                    /* of a normal */
    if (wide && exponent < minimum_exponent) {
        int subnormal_bits = f.fraction_bits - (minimum_exponent - exponent);
        if (subnormal_bits >= 0)
            wide = vsoftfloat_parse_with_precision(s, n, subnormal_bits);
        else if (subnormal_bits == -1) {
            /* about half the smallest subnormal 2^k (the coarse parse may
               have rounded up to it): the nearest is 2^k above 2^(k-1), 0 at
               or below it (exactly 2^(k-1) is a tie, to the even 0) */
            unsigned __int128 full = vsoftfloat_parse_with_precision(s, n, 112);
            int full_exponent = (int)((full >> 112) & 0x7FFF) - 16383;
            bool above_half =
                full_exponent > exponent ||
                (full_exponent == exponent &&
                 (full & (((unsigned __int128)1 << 112) - 1)) != 0);
            if (above_half)
                return 1;
            return 0;
        } else
            return 0;
    }
    return vsoftfloat_truncate(wide, f.fraction_bits, f.maximum_exponent,
                               f.bias);
}

} // namespace

/*
 * [lex.fcon]/1: "The type of a floating-point-literal is determined by its
 * floating-point-suffix as specified in Table 10":
 *
 *     none      double
 *     f or F    float
 *     l or L    long double
 *
 * and clang's f16 for _Float16, an extended floating-point type
 * ([basic.fundamental]/12) on this target.
 */
void lexer::convert_floating_point_literal(const pp_number_analysis &a,
                                           token &out)
{
    out.literal = literal_kind::floating_point_literal;
    if (a.float_suffix)
        out.type = fundamental_type::float_;
    else if (a.long_suffix)
        out.type = fundamental_type::long_double;
    else if (a.float16_suffix)
        out.type = fundamental_type::float16;
    else
        out.type = fundamental_type::double_;

    /*
     * [lex.fcon]/2: "In the significand, the sequence of digits or
     * hexadecimal-digits and optional period are interpreted as a base N real
     * number s ... Any optional separating single quotes are ignored when
     * determining the value. ... The scaled value of the literal is s x 10^e
     * for a decimal-floating-point-literal and s x 2^e for a
     * hexadecimal-floating-point-literal."
     */
    char significand_and_exponent[512];
    size_t n = 0;
    bool nonzero = false;
    bool in_exponent = false;
    for (size_t i = 0;
         i < a.suffix_begin && n + 1 < sizeof significand_and_exponent; i++) {
        char c = a.spelling[i];
        if (c == '\'')
            continue;
        if (i >= 2 && (c == 'e' || c == 'E' || c == 'p' || c == 'P') &&
            (a.base == 10 || c == 'p' || c == 'P'))
            in_exponent = true;
        if (!in_exponent &&
            ((c >= '1' && c <= '9') ||
             (a.base == 16 && i >= 2 &&
              is_hexadecimal_digit((unsigned char)c) && c != '0')))
            nonzero = true;
        significand_and_exponent[n++] = c;
    }
    if (a.suffix_begin >= sizeof significand_and_exponent)
        return; /* the value of a longer one is left unknown */

    /*
     * [lex.fcon]/3: "If the scaled value is not in the range of
     * representable values for its type, the program is ill-formed.
     * Otherwise, the value of a floating-point-literal is the scaled value if
     * representable, else the larger or smaller representable value nearest
     * the scaled value, chosen in an implementation-defined manner."  This
     * implementation chooses the nearest, ties to even (IEEE 754
     * roundTiesToEven), and, as clang, only warns out of range.
     */
    floating_point_format f = format_of(behavior, out.type);
    unsigned __int128 bits = round_to_format(significand_and_exponent, n, f);
    /* the literal has no sign: its bits are its magnitude */
    unsigned __int128 infinity = (unsigned __int128)f.maximum_exponent
                                 << f.fraction_bits;
    char type_name[32];
    int type_length = snprintf(type_name, sizeof type_name, "'%s'",
                               fundamental_type_name(out.type));
    if (bits == infinity)
        diagnostics
                .issue(diagnostic_id::warn_float_overflow,
                       out.preprocessing.location)
                .text(type_name, (size_t)type_length)
            << f.maximum;
    else if (bits == 0 && nonzero)
        diagnostics
                .issue(diagnostic_id::warn_float_underflow,
                       out.preprocessing.location)
                .text(type_name, (size_t)type_length)
            << f.minimum;
    literal_value &v = add_value(out);
    v.floating_bits_low = (uint64_t)bits;
    v.floating_bits_high = (uint64_t)(bits >> 64);
    out.value_known = true;
}

} // namespace cplusplus
