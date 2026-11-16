/*
 * lex.ppnumber.cpp: [lex.ppnumber] Preprocessing numbers.
 *
 *     pp-number:
 *         digit
 *         '.' digit
 *         pp-number digit
 *         pp-number identifier-nondigit
 *         pp-number ''' digit
 *         pp-number ''' nondigit
 *         pp-number 'e' sign
 *         pp-number 'E' sign
 *         pp-number 'p' sign
 *         pp-number 'P' sign
 *         pp-number '.'
 *
 * [lex.ppnumber]/2: "A preprocessing number does not have a type or a value;
 * it acquires both after a successful conversion to an integer-literal token
 * or a floating-point-literal token."  That is phase 7 (lex.icon.cpp).
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/* `at` is the first character (a digit, or the '.' before one); returns the
   end of the longest pp-number ([lex.pptoken]/3.3) */
uint32_t lexer::scan_pp_number(uint32_t at, bool *spelling_differs)
{
    const unsigned char *p = file.physical_characters;
    unsigned char previous = 0;
    bool hexadecimal = p[at] == '0' && (p[at + 1] == 'x' || p[at + 1] == 'X');
    for (;;) {
        unsigned char c = p[at];
        if (is_digit(c) || is_nondigit(c) || c == '.') {
            previous = c;
            at++;
            continue;
        }
        translation_character t = peek(at);
        if (!t.extent)
            return at;
        unsigned char next = t.character;
        bool more;
        if (is_digit(next) || is_nondigit(next) || next == '.')
            more = true;
        /* pp-number 'e' sign, 'E' sign, 'p' sign, 'P' sign.  Unlike the
           grammar, clang takes 'p' sign only in a pp-number that starts with
           0x (Lexer.cpp, isHexaLiteral), so 0p+3 is three preprocessing
           tokens there */
        else if ((next == '+' || next == '-') &&
                 (previous == 'e' || previous == 'E' ||
                  ((previous == 'p' || previous == 'P') && hexadecimal)))
            more = true;
        /* pp-number ''' digit, pp-number ''' nondigit: C++14's digit
           separators (N3781) */
        else if (next == '\'' &&
                 behavior.at_least(standard_edition::cplusplus_14)) {
            translation_character after = peek(at + t.extent);
            more = is_digit(after.character) || is_nondigit(after.character);
        } else if (next == '\\') {
            /* pp-number identifier-nondigit: a universal-character-name */
            uint32_t code_point,
                n = scan_universal_character_name(at, &code_point, false);
            if (!n || !is_identifier_continue(code_point))
                return at;
            scan_universal_character_name(at, &code_point, true);
            *spelling_differs |= n != 6 && n != 10;
            at += n;
            previous = 'x';
            continue;
        } else if (next >= 0x80) {
            uint32_t code_point, n = decode_utf8(p + at + t.extent - 1,
                                                 file.size - at, &code_point);
            if (!n || !is_identifier_continue(code_point))
                return at;
            at += t.extent - 1 + n;
            previous = 'x';
            continue;
        } else if (next == '$' && behavior.dollar_in_identifier) {
            more = true;
        } else
            more = false;
        if (!more)
            return at;
        if (t.extent != 1) {
            *spelling_differs = true;
            read(at);
        }
        previous = next;
        at += t.extent;
    }
}

} // namespace cplusplus
