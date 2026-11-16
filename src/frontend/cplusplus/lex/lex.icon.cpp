/*
 * lex.icon.cpp: [lex.icon] Integer literals: converting a pp-number to an
 * integer-literal token in phase 7, with its value and type.
 *
 *     integer-literal:
 *         binary-literal integer-suffix_opt
 *         octal-literal integer-suffix_opt
 *         decimal-literal integer-suffix_opt
 *         hexadecimal-literal integer-suffix_opt
 *
 *     binary-literal:
 *         '0b' binary-digit
 *         '0B' binary-digit
 *         binary-literal '''_opt binary-digit
 *
 *     octal-literal:
 *         '0'
 *         octal-literal '''_opt octal-digit
 *
 *     decimal-literal:
 *         nonzero-digit
 *         decimal-literal '''_opt digit
 *
 *     hexadecimal-literal:
 *         hexadecimal-prefix hexadecimal-digit-sequence
 *
 *     integer-suffix:
 *         unsigned-suffix long-suffix_opt
 *         unsigned-suffix long-long-suffix_opt
 *         long-suffix unsigned-suffix_opt
 *         long-long-suffix unsigned-suffix_opt
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

/* [lex.icon]/1, Table 7: the base N of each kind of integer-literal */
enum class integer_literal_form : uint8_t {
    binary = 2,
    octal = 8,
    decimal = 10,
    hexadecimal = 16
};

struct integer_suffix {
    bool unsigned_suffix = false;
    uint8_t long_suffixes = 0; /* 1: long-suffix, 2: long-long-suffix */
    /* C++23's size_t suffix (P0330R8): z or Z, with or without u */
    bool size_suffix = false;
};

/* integer-suffix, plus z/Z; false if the characters are not one */
bool parse_integer_suffix(const char *s, size_t n, integer_suffix *suffix)
{
    size_t i = 0;
    while (i < n) {
        char c = s[i];
        if ((c == 'u' || c == 'U') && !suffix->unsigned_suffix) {
            suffix->unsigned_suffix = true;
            i++;
        } else if ((c == 'l' || c == 'L') && !suffix->long_suffixes &&
                   !suffix->size_suffix) {
            /* long-long-suffix: one of 'll LL' (not lL or Ll) */
            if (i + 1 < n && s[i + 1] == c)
                suffix->long_suffixes = 2, i += 2;
            else
                suffix->long_suffixes = 1, i++;
        } else if ((c == 'z' || c == 'Z') && !suffix->size_suffix &&
                   !suffix->long_suffixes) {
            suffix->size_suffix = true;
            i++;
        } else
            return false;
    }
    return true;
}

/*
 * [lex.icon]/3, Table 8: "The type of an integer-literal is the first type
 * in the list ... corresponding to its optional integer-suffix in which its
 * value can be represented."  The list is fixed by the suffix and by whether
 * the literal is a decimal-literal: a decimal-literal never becomes unsigned
 * without a u.
 */
fundamental_type integer_literal_type(const implementation_defined_behavior &b,
                                      const integer_suffix &suffix,
                                      bool decimal, uint64_t value, bool *fits)
{
    static constexpr fundamental_type all[] = {
        fundamental_type::int_,
        fundamental_type::unsigned_int,
        fundamental_type::long_int,
        fundamental_type::unsigned_long_int,
        fundamental_type::long_long_int,
        fundamental_type::unsigned_long_long_int,
    };
    *fits = true;
    int first = suffix.long_suffixes * 2;
    for (int i = first; i < 6; i++) {
        fundamental_type t = all[i];
        bool is_signed = fundamental_type_is_signed(b, t);
        if (is_signed && suffix.unsigned_suffix)
            continue;
        if (!is_signed && decimal && !suffix.unsigned_suffix)
            continue;
        unsigned width = fundamental_type_width(b, t) - is_signed;
        if (width >= 64 || value >> width == 0)
            return t;
    }
    /* [lex.icon]/4: "If an integer-literal cannot be represented by any
       type in its list and an extended integer type can represent its
       value, it may have that extended integer type."  clang has none, and
       makes a decimal-literal that fits unsigned long long that type, with a
       warning; anything larger is ill-formed */
    *fits = false;
    return fundamental_type::unsigned_long_long_int;
}

} // namespace

/*
 * `spelling` is the pp-number's ([lex.ppnumber]/1: "Preprocessing number
 * tokens lexically include all integer-literal tokens").  Sets out's literal
 * kind, type and value, or issues the diagnostic that the pp-number is not
 * an integer-literal ([lex.pptoken]/1).
 */
void lexer::convert_integer_literal(const char *spelling, size_t length,
                                    token &out)
{
    source_location at = out.preprocessing.location;
    size_t i = 0;
    integer_literal_form form = integer_literal_form::decimal;

    if (length > 1 && spelling[0] == '0' &&
        (spelling[1] == 'x' || spelling[1] == 'X'))
        form = integer_literal_form::hexadecimal, i = 2;
    else if (length > 1 && spelling[0] == '0' &&
             (spelling[1] == 'b' || spelling[1] == 'B')) {
        form = integer_literal_form::binary, i = 2;
        /* binary-literal is C++14's (N3472), a clang extension before */
        if (!behavior.at_least(standard_edition::cplusplus_14))
            diagnostics.issue(diagnostic_id::ext_binary_literal_cxx14, at);
        else
            diagnostics.issue(diagnostic_id::warn_cxx11_compat_binary_literal,
                              at);
    } else if (spelling[0] == '0')
        form = integer_literal_form::octal, i = 1;

    /* [lex.icon]/1: "the sequence of binary-digits, octal-digits, digits,
       or hexadecimal-digits is interpreted as a base N integer ...; the
       lexically first digit of the sequence of digits is the most
       significant."  [lex.icon] Note 1: "The prefix and any optional
       separating single quotes are ignored when determining the value." */
    unsigned base = static_cast<unsigned>(form);
    uint64_t value = 0;
    bool overflow = false, digits = form == integer_literal_form::octal;
    size_t digits_start = i;
    for (; i < length; i++) {
        unsigned char c = (unsigned char)spelling[i];
        if (c == '\'') {
            /* '''_opt between two digits: never first or last */
            if (i == digits_start || i + 1 >= length ||
                !is_hexadecimal_digit((unsigned char)spelling[i + 1])) {
                diagnostics.issue(
                    diagnostic_id::err_digit_separator_not_between_digits,
                    at + (uint32_t)i)
                    << (int64_t)(i != digits_start);
                out.kind = token_kind::invalid;
                return;
            }
            continue;
        }
        unsigned d;
        if (is_digit(c))
            d = c - '0';
        else if (form == integer_literal_form::hexadecimal &&
                 is_hexadecimal_digit(c))
            d = (c | 32) - 'a' + 10;
        else
            break;
        if (d >= base) {
            /* an 8 or 9 in an octal-literal, a 2-9 in a binary-literal */
            char text[2] = {(char)c, 0};
            diagnostics
                    .issue(diagnostic_id::err_invalid_digit, at + (uint32_t)i)
                    .text(text, 1)
                << (int64_t)(form == integer_literal_form::octal ? 1 : 2);
            out.kind = token_kind::invalid;
            return;
        }
        if (value > (UINT64_MAX - d) / base)
            overflow = true;
        value = value * base + d;
        digits = true;
    }
    if (!digits) {
        /* 0x or 0b without digits: clang reports the suffix */
        out.kind = token_kind::invalid;
        diagnostics
                .issue(diagnostic_id::err_invalid_suffix_constant,
                       at + (uint32_t)digits_start)
                .text(spelling + digits_start - 1, length - digits_start + 1)
            << 0;
        return;
    }

    integer_suffix suffix;
    if (!parse_integer_suffix(spelling + i, length - i, &suffix)) {
        /* [lex.ext]/1: "If a token matches both user-defined-literal and
           another literal kind, it is treated as the latter."  Otherwise a
           suffix that is an identifier makes a user-defined-integer-literal
           (C++11); clang requires it to start with _ unless the standard
           library reserves it ([over.literal]/1, [usrlit.suffix]) */
        bool identifier = is_nondigit((unsigned char)spelling[i]) ||
                          (unsigned char)spelling[i] >= 0x80 ||
                          spelling[i] == '\\';
        if (behavior.at_least(standard_edition::cplusplus_11) && identifier) {
            out.literal = literal_kind::user_defined_literal;
            out.preprocessing.ud_suffix_offset = (uint32_t)i;
            return;
        }
        diagnostics
                .issue(diagnostic_id::err_invalid_suffix_constant,
                       at + (uint32_t)i)
                .text(spelling + i, length - i)
            << 0;
        out.kind = token_kind::invalid;
        return;
    }
    if (suffix.size_suffix) {
        if (behavior.at_least(standard_edition::cplusplus_23))
            diagnostics.issue(diagnostic_id::warn_cxx20_compat_size_t_suffix,
                              at);
        else
            diagnostics.issue(diagnostic_id::ext_cxx23_size_t_suffix, at);
    }

    out.literal = literal_kind::integer_literal;
    out.value = value;
    out.value_known = true;
    if (overflow) {
        /* [lex.icon]/4: "A program is ill-formed if one of its translation
           units contains an integer-literal that cannot be represented by
           any of the allowed types." */
        diagnostics.issue(diagnostic_id::err_integer_literal_too_large, at)
            << (int64_t)(!suffix.unsigned_suffix &&
                         form == integer_literal_form::decimal);
        out.type = fundamental_type::unsigned_long_long_int;
        return;
    }
    if (suffix.size_suffix) {
        /* [support.types.layout]: size_t, and its signed counterpart for z */
        out.type = suffix.unsigned_suffix ? fundamental_type::unsigned_long_int
                                          : fundamental_type::long_int;
        return;
    }
    bool fits;
    out.type = integer_literal_type(
        behavior, suffix, form == integer_literal_form::decimal, value, &fits);
    if (!fits)
        diagnostics.issue(
            diagnostic_id::ext_integer_literal_too_large_for_signed, at);
}

} // namespace cplusplus
