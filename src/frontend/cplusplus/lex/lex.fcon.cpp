/*
 * lex.fcon.cpp: [lex.fcon] Floating-point literals.  For now only which
 * pp-numbers are floating-point literals; the value ([lex.fcon]/2-3) is a
 * later step.
 *
 *     floating-point-literal:
 *         decimal-floating-point-literal
 *         hexadecimal-floating-point-literal
 *
 *     decimal-floating-point-literal:
 *         fractional-constant exponent-part_opt floating-point-suffix_opt
 *         digit-sequence exponent-part floating-point-suffix_opt
 *
 *     hexadecimal-floating-point-literal:
 *         hexadecimal-prefix hexadecimal-fractional-constant binary-exponent-part floating-point-suffix_opt
 *         hexadecimal-prefix hexadecimal-digit-sequence binary-exponent-part floating-point-suffix_opt
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/* a fractional-constant (a '.') or an exponent-part (e after decimal digits,
   p after hexadecimal ones) makes a floating-point-literal */
VPRIV bool is_floating_point_literal_spelling(const char *spelling,
                                              size_t length)
{
    bool hexadecimal = length > 1 && spelling[0] == '0' &&
                       (spelling[1] == 'x' || spelling[1] == 'X');
    for (size_t i = hexadecimal ? 2 : 0; i < length; i++) {
        char c = spelling[i];
        if (c == '.')
            return true;
        if (hexadecimal ? (c == 'p' || c == 'P') : (c == 'e' || c == 'E'))
            /* an exponent-part: 'e' sign_opt digit-sequence; an e followed by
               anything else starts a suffix (TODO: clang's
               err_exponent_has_no_digits for 1e) */
            return i + 1 < length &&
                   (is_digit((unsigned char)spelling[i + 1]) ||
                    spelling[i + 1] == '+' || spelling[i + 1] == '-');
        if (!(hexadecimal ? is_hexadecimal_digit((unsigned char)c)
                          : is_digit((unsigned char)c)) &&
            c != '\'')
            return false;
    }
    return false;
}

} // namespace cplusplus
