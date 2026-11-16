/*
 * lex.icon.cpp: [lex.icon] Integer literals: the value and type of a
 * pp-number that lex.ppnumber.cpp read as an integer-literal.
 *
 *     integer-literal:
 *         binary-literal integer-suffix_opt
 *         octal-literal integer-suffix_opt
 *         decimal-literal integer-suffix_opt
 *         hexadecimal-literal integer-suffix_opt
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

/*
 * [lex.icon]/3: "The type of an integer-literal is the first type in the
 * list in Table 8 corresponding to its optional integer-suffix in which its
 * value can be represented."  The lists, by suffix, for a decimal-literal
 * and for the other integer-literals:
 *
 *     none      int, long int, long long int
 *               int, unsigned int, long int, unsigned long int, long long int, unsigned long long int
 *     u or U    unsigned int, unsigned long int, unsigned long long int
 *     l or L    long int, long long int
 *               long int, unsigned long int, long long int, unsigned long long int
 *     u and l   unsigned long int, unsigned long long int
 *     ll or LL  long long int
 *               long long int, unsigned long long int
 *     u and ll  unsigned long long int
 *
 * Each list is the six types from the suffix's rank on, without the unsigned
 * ones for a decimal-literal without u, without the signed ones with u.
 */
fundamental_type first_type_in_list(const implementation_defined_behavior &b,
                                    const lexer::pp_number_analysis &a,
                                    uint64_t value, bool *represented)
{
    static constexpr fundamental_type types[] = {
        fundamental_type::int_,
        fundamental_type::unsigned_int,
        fundamental_type::long_int,
        fundamental_type::unsigned_long_int,
        fundamental_type::long_long_int,
        fundamental_type::unsigned_long_long_int,
    };
    bool decimal = a.base == 10;
    *represented = true;
    for (int i = a.long_long_suffix ? 4 : a.long_suffix ? 2 : 0; i < 6; i++) {
        fundamental_type t = types[i];
        bool is_signed = fundamental_type_is_signed(b, t);
        if (is_signed ? a.unsigned_suffix : decimal && !a.unsigned_suffix)
            continue;
        unsigned value_bits = fundamental_type_width(b, t) - is_signed;
        if (value_bits >= 64 || value >> value_bits == 0)
            return t;
    }
    *represented = false;
    return fundamental_type::unsigned_long_long_int;
}

} // namespace

void lexer::convert_integer_literal(const pp_number_analysis &a, token &out)
{
    source_location at = out.preprocessing.location;

    /* [lex.icon]/1: "the sequence of binary-digits, octal-digits, digits,
       or hexadecimal-digits is interpreted as a base N integer as shown in
       table Table 7; the lexically first digit of the sequence of digits is
       the most significant."  [lex.icon]/1 Note 1: "The prefix and any
       optional separating single quotes are ignored when determining the
       value."  [lex.icon]/2: "The hexadecimal-digits a through f and A
       through F have decimal values ten through fifteen." */
    uint64_t value = 0;
    bool too_large = false;
    unsigned value_bits = 0; /* bits of the value, for _BitInt(N) */
    for (size_t i = a.digits_begin; i < a.suffix_begin; i++) {
        unsigned char c = (unsigned char)a.spelling[i];
        if (c == '\'')
            continue;
        unsigned digit = is_digit(c) ? c - '0' : (c | 32) - 'a' + 10;
        if (value > (UINT64_MAX - digit) / a.base)
            too_large = true;
        value = value * a.base + digit;
    }
    for (uint64_t v = value; v; v >>= 1)
        value_bits++;
    out.literal = literal_kind::integer_literal;
    out.value = value;
    out.value_known = true;

    if (a.bit_precise_suffix) {
        /* C23 6.4.4.1: the width is the smallest that holds the value (and a
           sign bit for the signed _BitInt(N), whose N is at least 2); the
           value of a wider one is not kept yet */
        out.type = a.unsigned_suffix
                       ? fundamental_type::unsigned_bit_precise_int
                       : fundamental_type::bit_precise_int;
        out.bit_precise_width =
            (uint16_t)(too_large ? 0 : value_bits + !a.unsigned_suffix);
        if (out.bit_precise_width < (a.unsigned_suffix ? 1 : 2))
            out.bit_precise_width = a.unsigned_suffix ? 1 : 2;
        out.value_known = !too_large;
        return;
    }
    if (too_large) {
        /* [lex.icon]/4: "A program is ill-formed if one of its translation
           units contains an integer-literal that cannot be represented by
           any of the allowed types."  No extended integer type is wider than
           unsigned long long here. */
        diagnostics.issue(a.size_suffix
                              ? diagnostic_id::err_size_t_literal_too_large
                              : diagnostic_id::err_integer_literal_too_large,
                          at)
            << 1;
        out.type = fundamental_type::unsigned_long_long_int;
        out.value_known = false;
        return;
    }
    if (a.size_suffix) {
        /* P0330R8 (C++23): z is the signed integer type corresponding to
           std::size_t, uz is std::size_t */
        diagnostics.issue(behavior.has(language_feature::size_t_suffix)
                              ? diagnostic_id::warn_cxx20_compat_size_t_suffix
                              : diagnostic_id::ext_cxx23_size_t_suffix,
                          at);
        out.type = a.unsigned_suffix ? fundamental_type::unsigned_long_int
                                     : fundamental_type::long_int;
        if (!a.unsigned_suffix && value >> 63)
            diagnostics.issue(diagnostic_id::err_size_t_literal_too_large, at)
                << 0;
        return;
    }
    bool represented;
    out.type = first_type_in_list(behavior, a, value, &represented);
    if (!represented)
        /* [lex.icon]/4: "If an integer-literal cannot be represented by any
           type in its list and an extended integer type can represent its
           value, it may have that extended integer type."  clang uses
           unsigned long long, as if the list had it, with a warning. */
        diagnostics.issue(
            diagnostic_id::ext_integer_literal_too_large_for_signed, at);
}

} // namespace cplusplus
