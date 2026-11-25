/*
 * pp-number.cpp: [lex.ppnumber] Preprocessing numbers: where one ends
 * (phase 3), and its analysis into the parts of an integer-literal or a
 * floating-point-literal (phase 7), which integer-literal.cpp and
 * floating-point-literal.cpp then give a type and a value.
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
 * [lex.ppnumber]/1: "Preprocessing number tokens lexically include all
 * integer-literal tokens and all floating-point-literal tokens."
 * [lex.ppnumber]/2: "A preprocessing number does not have a type or a value;
 * it acquires both after a successful conversion to an integer-literal token
 * or a floating-point-literal token."
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * The longest pp-number from `at` (a digit, or the '.' before one):
 * [lex.pptoken]/3.3, "the longest sequence of characters that could
 * constitute a preprocessing token".
 *
 * "pp-number 'p' sign" is C++17's, [diff.cpp14.lex]/2: "character sequences
 * like 0p+0 and 0e1_p+0 are three separate tokens each in C++ 2014, but one
 * single token in this International Standard."  Before C++17 this
 * implementation takes 'p' sign as an extension ([intro.compliance]/8) for
 * hexadecimal floating-point literals, only where no C++14 program is
 * well-formed: after 0x, before any '_' (0x1_p+1 is 0x1_p + 1, a
 * user-defined literal and an addition).
 */
uint32_t lexer::scan_pp_number(uint32_t at, bool *spelling_differs)
{
    const unsigned char *p = file.physical_characters;
    translation_character first = peek(at), second = peek(at + first.extent);
    bool hexadecimal = first.character == '0' &&
                       (second.character == 'x' || second.character == 'X');
    bool underscore = false;
    unsigned char previous = 0;

    for (;;) {
        translation_character t = peek(at);
        if (!t.extent)
            return at;
        unsigned char c = t.character;
        bool take = false;

        /* pp-number digit, pp-number identifier-nondigit, pp-number '.' */
        if (is_digit(c) || is_nondigit(c) || c == '.')
            take = true;
        /* pp-number 'e' sign, 'E' sign, 'p' sign, 'P' sign */
        else if ((c == '+' || c == '-') && (previous == 'e' || previous == 'E'))
            take = true;
        else if ((c == '+' || c == '-') && (previous == 'p' || previous == 'P'))
            take = behavior.has(
                       language_feature::hexadecimal_floating_point_literals) ||
                   (hexadecimal && !underscore);
        else if (c == '\'' &&
                 behavior.has(language_feature::digit_separators)) {
            /* pp-number ''' digit, pp-number ''' nondigit: C++14's digit
               separators (N3781) */
            translation_character after = peek(at + t.extent);
            if (is_digit(after.character) || is_nondigit(after.character)) {
                diagnostics.issue(
                    diagnostic_id::warn_cxx11_compat_digit_separator, at);
                if (t.extent != 1)
                    read(at);
                *spelling_differs |= t.extent != 1 || after.extent != 1;
                underscore |= after.character == '_';
                at += t.extent + after.extent;
                previous = after.character;
                continue;
            }
        } else if (c == '$' && behavior.dollar_in_identifier) {
            /* clang's extension: '$' is an identifier-nondigit */
            take = true;
        } else if (c == '\\') {
            /* pp-number identifier-nondigit: a universal-character-name */
            uint32_t code_point,
                n = scan_universal_character_name(at, &code_point, false);
            if (!n || !is_identifier_continue(code_point))
                return at;
            scan_universal_character_name(at, &code_point, true);
            /* \uXXXX is 6 characters, \UXXXXXXXX 10 */
            uint32_t written = 6;
            if (code_point > 0xFFFF)
                written = 10;
            *spelling_differs |= n != (t.extent - 1) + written;
            at += n;
            previous = 0;
            continue;
        } else if (c >= 0x80) {
            uint32_t code_point, n = decode_utf8(p + at + t.extent - 1,
                                                 file.size - at, &code_point);
            if (!n || !is_identifier_continue(code_point))
                return at;
            at += t.extent - 1 + n;
            previous = 0;
            continue;
        }
        if (!take)
            return at;
        if (t.extent != 1) {
            *spelling_differs = true;
            read(at);
        }
        underscore |= c == '_';
        previous = c;
        at += t.extent;
    }
}

/*
 * [lex.ext]/1: "If a token matches both user-defined-literal and another
 * literal kind, it is treated as the latter."  So a suffix is first tried as
 * an integer-suffix or floating-point-suffix, and only then as a ud-suffix.
 * [over.literal]/1 reserves the ud-suffixes that do not begin with an
 * underscore for the standard library; as clang, only those the library uses
 * are taken, any other is an invalid suffix.
 */
VPRIV bool is_valid_ud_suffix(const implementation_defined_behavior &b,
                              const char *s, size_t n)
{
    if (!b.has(language_feature::user_defined_literals) || !n)
        return false;
    if (s[0] == '_')
        return n < 2 || s[1] != '_';
    if (!b.has(language_feature::library_ud_suffixes))
        return false;
    static constexpr const char *library_suffixes[] = {
        "h", "min", "s", "ms", "us", "ns", "il", "i", "if"};
    for (const char *l : library_suffixes)
        if (strlen(l) == n && !memcmp(l, s, n))
            return true;
    /* C++20's chrono literals d and y */
    return b.has(language_feature::chrono_ud_suffixes) && n == 1 &&
           (s[0] == 'd' || s[0] == 'y');
}

/*
 * The parts of a pp-number by the grammars of [lex.icon] and [lex.fcon]:
 * prefix, digits, fractional part, exponent, suffix, left to right.  Returns
 * false after a diagnostic if the pp-number is neither literal
 * ([lex.pptoken]/1).
 */
bool lexer::analyze_pp_number(const char *s, size_t n, source_location at,
                              pp_number_analysis &a)
{
    a = pp_number_analysis{};
    a.spelling = s;
    a.length = n;
    size_t i = 0;

    /* "'''_opt digit": a digit separator follows a digit and is followed by
       one; reported where a sequence of digits starts or ends with one */
    auto check_separator = [&](size_t where, bool after_digits) {
        size_t q = where;
        if (after_digits) {
            if (where == 0)
                return;
            q = where - 1;
        } else if (where >= n)
            return;
        if (s[q] != '\'')
            return;
        diagnostics.issue(diagnostic_id::err_digit_separator_not_between_digits,
                          at + (uint32_t)q)
            << (int64_t)after_digits;
        a.error = true;
    };
    auto skip_digits = [&](size_t j, unsigned base) {
        for (; j < n; j++) {
            unsigned char c = (unsigned char)s[j];
            bool digit;
            if (base == 16)
                digit = is_hexadecimal_digit(c);
            else
                digit = is_digit(c) && (unsigned)(c - '0') < base;
            if (!digit && c != '\'')
                break;
        }
        return j;
    };
    auto contains_digits = [&](size_t from, size_t to) {
        for (size_t j = from; j < to; j++)
            if (s[j] != '\'')
                return true;
        return false;
    };
    auto invalid_digit = [&](size_t j, int form) {
        char digit = s[j];
        diagnostics.issue(diagnostic_id::err_invalid_digit, at + (uint32_t)j)
                .text(&digit, 1)
            << form;
        a.error = true;
    };
    /*
     *     exponent-part:
     *         'e' sign_opt digit-sequence
     *         'E' sign_opt digit-sequence
     *
     *     binary-exponent-part:
     *         'p' sign_opt digit-sequence
     *         'P' sign_opt digit-sequence
     */
    auto exponent_part = [&](size_t j) {
        check_separator(j, true);
        size_t exponent = j++;
        a.exponent = true;
        if (j < n && (s[j] == '+' || s[j] == '-'))
            j++;
        size_t end = skip_digits(j, 10);
        if (!contains_digits(j, end)) {
            if (!a.error)
                diagnostics.issue(diagnostic_id::err_exponent_has_no_digits,
                                  at + (uint32_t)exponent);
            a.error = true;
            return j;
        }
        check_separator(j, false);
        return end;
    };
    /* what follows the digits of a decimal-literal or octal-literal: a
       fractional-constant's period and an exponent-part make it a
       decimal-floating-point-literal */
    auto decimal_or_octal_rest = [&](size_t j) {
        if (j < n && is_hexadecimal_digit((unsigned char)s[j]) && s[j] != 'e' &&
            s[j] != 'E' && !is_valid_ud_suffix(behavior, s + j, n - j)) {
            invalid_digit(j, a.base == 8);
            return j;
        }
        if (j < n && s[j] == '.') {
            check_separator(j, true);
            j++;
            a.base = 10;
            a.period = true;
            check_separator(j, false);
            j = skip_digits(j, 10);
        }
        if (j < n && (s[j] == 'e' || s[j] == 'E')) {
            a.base = 10;
            j = exponent_part(j);
        }
        return j;
    };

    if (s[0] != '0') {
        /*
         * decimal-literal:
         *     nonzero-digit
         *     decimal-literal '''_opt digit
         */
        a.base = 10;
        i = skip_digits(0, 10);
        if (i < n)
            i = decimal_or_octal_rest(i);
    } else if ((s[1] == 'x' || s[1] == 'X') &&
               (is_hexadecimal_digit((unsigned char)s[2]) || s[2] == '.')) {
        /*
         * hexadecimal-literal:
         *     hexadecimal-prefix hexadecimal-digit-sequence
         *
         * hexadecimal-floating-point-literal:
         *     hexadecimal-prefix hexadecimal-fractional-constant binary-exponent-part floating-point-suffix_opt
         *     hexadecimal-prefix hexadecimal-digit-sequence binary-exponent-part floating-point-suffix_opt
         */
        a.base = 16;
        a.digits_begin = i = 2;
        i = skip_digits(i, 16);
        bool significand_digits = contains_digits(2, i);
        if (i < n && s[i] == '.') {
            a.period = true;
            size_t fraction = ++i;
            i = skip_digits(i, 16);
            significand_digits |= contains_digits(fraction, i);
            if (significand_digits)
                check_separator(fraction, false);
        }
        if (!significand_digits) {
            /* [lex.fcon]: the significand needs a hexadecimal-digit */
            diagnostics.issue(diagnostic_id::err_hex_constant_requires,
                              at + (uint32_t)i)
                << 1 << 1;
            return false;
        }
        if (i < n && (s[i] == 'p' || s[i] == 'P')) {
            i = exponent_part(i);
            if (a.error)
                return false;
            /* hexadecimal floating-point literals are C++17's (P0245R1);
               clang takes them as an extension in the GNU modes */
            if (!behavior.has(
                    language_feature::hexadecimal_floating_point_literals) &&
                !behavior.gnu_extensions)
                diagnostics.issue(diagnostic_id::ext_hex_literal_invalid, at);
            else if (behavior.has(
                         language_feature::hexadecimal_floating_point_literals))
                diagnostics.issue(diagnostic_id::warn_cxx17_hex_literal, at);
        } else if (a.period) {
            /* "hexadecimal-prefix hexadecimal-fractional-constant
               binary-exponent-part": the exponent is not optional */
            diagnostics.issue(diagnostic_id::err_hex_constant_requires,
                              at + (uint32_t)i)
                << 1 << 0;
            a.error = true;
        }
    } else if ((s[1] == 'b' || s[1] == 'B') && (s[2] == '0' || s[2] == '1')) {
        /*
         * binary-literal:
         *     '0b' binary-digit
         *     '0B' binary-digit
         *     binary-literal '''_opt binary-digit
         *
         * C++14's (N3472), an extension before.
         */
        if (behavior.has(language_feature::binary_literals))
            diagnostics.issue(diagnostic_id::warn_cxx11_compat_binary_literal,
                              at);
        else
            diagnostics.issue(diagnostic_id::ext_binary_literal_cxx14, at);
        a.base = 2;
        a.digits_begin = i = 2;
        i = skip_digits(i, 2);
        if (i < n && is_hexadecimal_digit((unsigned char)s[i]) &&
            !is_valid_ud_suffix(behavior, s + i, n - i))
            invalid_digit(i, 2);
    } else {
        /*
         * octal-literal:
         *     '0'
         *     octal-literal '''_opt octal-digit
         *
         * or a decimal-floating-point-literal that starts with 0: 0.5, 09e1.
         */
        a.base = 8;
        i = skip_digits(1, 8);
        a.digits_begin = i != 1;
        if (i < n && is_digit((unsigned char)s[i])) {
            size_t end = skip_digits(i, 10);
            if (end < n && (s[end] == '.' || s[end] == 'e' || s[end] == 'E')) {
                i = end;
                a.base = 10;
            }
        }
        if (i < n)
            i = decimal_or_octal_rest(i);
    }
    if (a.error)
        return false;

    a.suffix_begin = i;
    check_separator(i, true);
    if (a.error)
        return false;
    a.floating = a.period || a.exponent;

    /*
     *     integer-suffix:
     *         unsigned-suffix long-suffix_opt
     *         unsigned-suffix long-long-suffix_opt
     *         long-suffix unsigned-suffix_opt
     *         long-long-suffix unsigned-suffix_opt
     *
     *     floating-point-suffix: one of
     *         'f l F L'
     *
     * with clang's extensions: z for size_t (P0330R8, C++23), f16 for
     * _Float16, the Microsoft integer suffixes i8 to i128, and i and j for
     * GNU imaginary constants.
     */
    bool has_size = false;
    for (; i < n; i++) {
        char c = s[i];
        if ((c == 'f' || c == 'F') && a.floating && !has_size) {
            has_size = true;
            if (i + 2 < n && s[i + 1] == '1' && s[i + 2] == '6') {
                i += 2;
                a.float16_suffix = true;
            } else
                a.float_suffix = true;
            continue;
        }
        if ((c == 'u' || c == 'U') && !a.floating && !a.unsigned_suffix) {
            a.unsigned_suffix = true;
            continue;
        }
        if ((c == 'l' || c == 'L') && !has_size) {
            has_size = true;
            if (i + 1 < n && s[i + 1] == c) {
                if (a.floating)
                    break;
                a.long_long_suffix = true;
                i++;
            } else
                a.long_suffix = true;
            continue;
        }
        if ((c == 'z' || c == 'Z') && !a.floating && !has_size) {
            has_size = true;
            a.size_suffix = true;
            continue;
        }
        /* __wb and __uwb: clang's bit-precise integer literals in C++, C23's
           wb and uwb behind the implementation's reserved prefix */
        if (c == '_' && !a.floating && !has_size && i + 2 < n &&
            s[i + 1] == '_') {
            size_t j = i + 2;
            bool is_unsigned = false;
            if (j + 1 < n && (s[j] == 'u' || s[j] == 'U'))
                is_unsigned = true, j++;
            if (j + 1 < n && ((s[j] == 'w' && s[j + 1] == 'b') ||
                              (s[j] == 'W' && s[j + 1] == 'B'))) {
                has_size = a.bit_precise_suffix = true;
                a.unsigned_suffix |= is_unsigned;
                i = j + 1;
                continue;
            }
            break;
        }
        if ((c == 'i' || c == 'I') && behavior.microsoft_extensions &&
            !a.floating && i + 1 < n) {
            static constexpr struct {
                const char *digits;
                uint8_t bits;
            } microsoft[] = {
                {"8", 8}, {"16", 16}, {"32", 32}, {"64", 64}, {"128", 128}};
            bool matched = false;
            for (const auto &m : microsoft) {
                size_t k = strlen(m.digits);
                if (i + k < n + 1 && !memcmp(s + i + 1, m.digits, k)) {
                    if (has_size)
                        break;
                    has_size = matched = true;
                    a.microsoft_integer_bits = m.bits;
                    i += k + 1;
                    break;
                }
            }
            if (matched)
                break;
        }
        if ((c == 'i' || c == 'I' || c == 'j' || c == 'J') && !a.imaginary) {
            a.imaginary = true;
            continue;
        }
        break;
    }

    if (i != n || a.imaginary) {
        if (is_valid_ud_suffix(behavior, s + a.suffix_begin,
                               n - a.suffix_begin)) {
            if (!a.imaginary) {
                a.unsigned_suffix = a.long_suffix = a.long_long_suffix =
                    a.size_suffix = false;
                a.float_suffix = a.float16_suffix = a.bit_precise_suffix =
                    false;
                a.microsoft_integer_bits = 0;
            }
            a.ud_suffix = true;
            return true;
        }
        if (i != n) {
            diagnostics
                    .issue(diagnostic_id::err_invalid_suffix_constant,
                           at + (uint32_t)a.suffix_begin)
                    .text(s + a.suffix_begin, n - a.suffix_begin)
                << (int64_t)a.floating;
            return false;
        }
    }
    return true;
}

} // namespace cplusplus
