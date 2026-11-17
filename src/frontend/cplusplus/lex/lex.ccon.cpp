/*
 * lex.ccon.cpp: [lex.ccon] Character literals: the meaning of a c-char
 * (also of an s-char, [lex.string]/13), and the value and type of a
 * character-literal (translation phases 5 and 7).
 *
 *     character-literal:
 *         encoding-prefix_opt ''' c-char-sequence '''
 *
 *     c-char:
 *         any member of the basic source character set except the single-quote ''', backslash '\', or new-line character
 *         escape-sequence
 *         universal-character-name
 *
 *     escape-sequence:
 *         simple-escape-sequence
 *         octal-escape-sequence
 *         hexadecimal-escape-sequence
 *
 * The execution character set ([lex.charset]/3) of this implementation is
 * Unicode: UTF-8 for char and char8_t, UTF-16 for char16_t, UTF-32 for
 * char32_t and the 32-bit wchar_t (UTF-16 for a 16-bit one).
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/* the value of a hexadecimal-digit */
static unsigned escape_digit_value(unsigned char h)
{
    if (is_digit(h))
        return h - '0';
    return (unsigned)((h | 32) - 'a' + 10);
}

/* the largest value one code unit of an encoding holds */
VPRIV uint32_t largest_code_unit(const implementation_defined_behavior &b,
                                 encoding_prefix prefix)
{
    switch (prefix) {
    case encoding_prefix::u:
        return 0xFFFF;
    case encoding_prefix::U:
        return 0xFFFFFFFF;
    case encoding_prefix::L:
        if (b.wchar_t_width >= 32)
            return 0xFFFFFFFF;
        return (1u << b.wchar_t_width) - 1;
    default:
        return 0xFF;
    }
}

/*
 * One c-char or s-char at spelling[*i], which is not the closing quote:
 * advances *i past it.  An escape-sequence names a code unit, a
 * universal-character-name or an extended character names a code point, the
 * encoding turns into one or more code units ([lex.ccon]/8, [lex.string]/13).
 * Diagnostics are at `at` plus the offset in the spelling.
 */
lexer::c_char lexer::evaluate_c_char(const char *s, size_t n, size_t *i,
                                     source_location at, encoding_prefix prefix)
{
    const implementation_defined_behavior &b = behavior;
    diagnostic_consumer &d = diagnostics;
    c_char c;
    size_t start = *i;
    source_location here = at + (uint32_t)start;
    unsigned char first = (unsigned char)s[start];

    /* the rules for the character a universal-character-name designates
       ([lex.charset]/2), for \u, \U and \N alike */
    auto designated = [&](uint32_t value) {
        /* [lex.charset]/2: "The program is ill-formed if that number is not
           a code point or if it is a surrogate code point." */
        if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
            d.issue(diagnostic_id::err_ucn_escape_invalid, here);
            c.error = true;
            return c;
        }
        /* [lex.charset]/2: "If a universal-character-name outside the
           c-char-sequence, s-char-sequence, or r-char-sequence of a
           character-literal or string-literal ... corresponds to a control
           character or to a character in the basic source character set,
           the program is ill-formed."  Inside a literal it is allowed since
           C++11; C++03 forbade it everywhere ($ @ ` are not basic). */
        if (value < 0xA0 && value != '$' && value != '@' && value != '`') {
            bool basic = value >= 0x20 && value < 0x7F;
            char text = (char)value;
            if (!b.has(
                    language_feature::
                        universal_character_names_for_basic_characters_in_literals)) {
                if (basic)
                    d.issue(diagnostic_id::err_ucn_escape_basic_scs, here)
                        .text(&text, 1);
                else
                    d.issue(diagnostic_id::err_ucn_control_character, here);
                c.error = true;
                return c;
            }
            if (basic)
                d.issue(diagnostic_id::
                            warn_cxx98_compat_literal_ucn_escape_basic_scs,
                        here)
                    .text(&text, 1);
            else
                d.issue(diagnostic_id::
                            warn_cxx98_compat_literal_ucn_control_character,
                        here);
        }
        c.value = value;
        c.is_code_point = true;
        return c;
    };

    if (first != '\\') {
        /* a member of the basic source character set, or an extended
           character: [lex.phases]/1.1 made it a universal-character-name */
        uint32_t code_point;
        uint32_t length =
            decode_utf8(reinterpret_cast<const unsigned char *>(s + start),
                        (uint32_t)(n - start), &code_point);
        if (!length) {
            /* not UTF-8: the byte as it is, as clang (with a warning for an
               ordinary literal, an error otherwise) */
            c.invalid_encoding = true;
            c.value = first;
            *i = start + 1;
            return c;
        }
        c.value = code_point;
        c.is_code_point = true;
        *i = start + length;
        return c;
    }

    unsigned char e = 0;
    if (start + 1 < n)
        e = (unsigned char)s[start + 1];
    *i = start + 2;
    switch (e) {
    /* [lex.ccon]/6, Table 9: simple-escape-sequence */
    case '\'':
    case '"':
    case '?':
    case '\\':
        c.value = e;
        return c;
    case 'a':
        c.value = 7;
        return c;
    case 'b':
        c.value = 8;
        return c;
    case 'f':
        c.value = 12;
        return c;
    case 'n':
        c.value = 10;
        return c;
    case 'r':
        c.value = 13;
        return c;
    case 't':
        c.value = 9;
        return c;
    case 'v':
        c.value = 11;
        return c;
    case 'e':
    case 'E': {
        /* "Escape sequences in which the character following the backslash
           is not listed in Table 9 are conditionally-supported": GNU's \e is
           ESC */
        char text = (char)e;
        d.issue(diagnostic_id::ext_nonstandard_escape, here).text(&text, 1);
        c.value = 27;
        return c;
    }
    case 'u':
    case 'U': {
        /* [lex.charset]/2 universal-character-name, and from C++23 \u{...} */
        size_t j = start + 2;
        uint32_t value = 0, digits = 0, want = 8;
        if (e == 'u')
            want = 4;
        bool delimited = e == 'u' && j < n && s[j] == '{';
        if (delimited)
            j++;
        while (j < n && is_hexadecimal_digit((unsigned char)s[j]) &&
               (delimited || digits < want)) {
            unsigned char h = (unsigned char)s[j++];
            if (value > 0x10FFFF)
                value = 0x110000;
            else
                value = value << 4 | escape_digit_value(h);
            digits++;
        }
        if (delimited) {
            if (j >= n || s[j] != '}') {
                d.issue(diagnostic_id::err_delimited_escape_missing_brace, here)
                    .text("u", 1);
                c.error = true;
                *i = j;
                return c;
            }
            j++;
            if (!digits) {
                d.issue(diagnostic_id::err_delimited_escape_empty, here);
                c.error = true;
                *i = j;
                return c;
            }
            if (!b.has(language_feature::delimited_escape_sequences))
                d.issue(diagnostic_id::ext_delimited_escape_sequence, at)
                    << 0 << 0;
        } else if (digits != want) {
            /* "'\u' hex-quad": no hexadecimal-digit at all, or too few */
            char kind = (char)e;
            if (!digits)
                d.issue(diagnostic_id::err_hex_escape_no_digits, here)
                    .text(&kind, 1);
            else
                d.issue(diagnostic_id::err_ucn_escape_incomplete, here);
            c.error = true;
            *i = j;
            return c;
        }
        *i = j;
        return designated(value);
    }
    case 'N': {
        /*
         * P2071R2 (C++23), clang's extension before:
         *
         *     named-universal-character:
         *         '\N{' n-char-sequence '}'
         */
        size_t j = start + 2;
        if (j >= n || s[j] != '{') {
            d.issue(diagnostic_id::err_delimited_escape_missing_brace, here)
                .text("N", 1);
            c.error = true;
            return c;
        }
        size_t close = j + 1;
        while (close < n && s[close] != '}')
            close++;
        if (close >= n) {
            d.issue(diagnostic_id::err_delimited_escape_missing_brace, here)
                .text("N", 1);
            c.error = true;
            *i = close;
            return c;
        }
        *i = close + 1;
        if (close == j + 1) {
            d.issue(diagnostic_id::err_delimited_escape_empty, here);
            c.error = true;
            return c;
        }
        uint32_t value = code_point_of_unicode_name(s + j + 1, close - j - 1);
        if (value == UINT32_MAX) {
            /* at the name, after \N{ */
            d.issue(diagnostic_id::err_invalid_ucn_name, at + (uint32_t)(j + 1))
                .text(s + j + 1, close - j - 1);
            c.error = true;
            return c;
        }
        /* an extension before C++23, diagnosed at the literal */
        if (!b.has(language_feature::named_universal_character_names))
            d.issue(diagnostic_id::ext_delimited_escape_sequence, at) << 1 << 0;
        return designated(value);
    }
    case 'x':
    case 'o': {
        /* [lex.ccon]/7: "The escape \xhhh consists of the backslash followed
           by x followed by one or more hexadecimal digits that are taken to
           specify the value of the desired character.  There is no limit to
           the number of digits in a hexadecimal sequence."  \x{...} and
           \o{...} are C++23's (P2290R3). */
        size_t j = start + 2;
        bool delimited = j < n && s[j] == '{';
        unsigned base = 8;
        if (e == 'x')
            base = 16;
        if (e == 'o' && !delimited) {
            d.issue(diagnostic_id::err_delimited_escape_missing_brace, here)
                .text("o", 1);
            c.error = true;
            return c;
        }
        if (delimited)
            j++;
        uint64_t value = 0;
        bool overflow = false;
        size_t digits = 0;
        for (; j < n; j++) {
            unsigned char h = (unsigned char)s[j];
            unsigned v;
            if (base == 16 && is_hexadecimal_digit(h))
                v = escape_digit_value(h);
            else if (base == 8 && h >= '0' && h <= '7')
                v = h - '0';
            else
                break;
            overflow |= value >> 60 != 0;
            value = value * base + v;
            digits++;
        }
        if (delimited) {
            if (j >= n || s[j] != '}') {
                char bad = 0;
                if (j < n)
                    bad = s[j];
                d.issue(diagnostic_id::err_delimited_escape_invalid,
                        at + (uint32_t)j)
                    .text(&bad, 1);
                c.error = true;
                *i = j;
                return c;
            }
            j++;
            if (!digits) {
                d.issue(diagnostic_id::err_delimited_escape_empty, here);
                c.error = true;
                *i = j;
                return c;
            }
            if (!b.has(language_feature::delimited_escape_sequences))
                d.issue(diagnostic_id::ext_delimited_escape_sequence, at)
                    << 0 << 0;
        } else if (!digits) {
            d.issue(diagnostic_id::err_hex_escape_no_digits, here).text("x", 1);
            c.error = true;
            *i = j;
            return c;
        }
        *i = j;
        /* [lex.ccon]/7 Note 4: a value outside the range of the type is
           ill-formed for u8, u, U; implementation-defined (an error here, as
           clang) for the others */
        if (overflow || value > largest_code_unit(b, prefix)) {
            d.issue(diagnostic_id::err_escape_too_large, here)
                << (int)(base != 16);
            c.error = true;
        }
        c.value = (uint32_t)value;
        return c;
    }
    default:
        if (e >= '0' && e <= '7') {
            /* [lex.ccon]/7: "The escape \ooo consists of the backslash
               followed by one, two, or three octal digits" */
            size_t j = start + 1;
            uint32_t value = 0;
            for (int k = 0; k < 3 && j < n && s[j] >= '0' && s[j] <= '7'; k++)
                value = value * 8 + (uint32_t)(s[j++] - '0');
            *i = j;
            if (value > largest_code_unit(b, prefix)) {
                d.issue(diagnostic_id::err_escape_too_large, here) << 1;
                c.error = true;
            }
            c.value = value;
            return c;
        }
        /* [lex.ccon]/6: conditionally-supported; clang warns and takes the
           character itself */
        char text = (char)e;
        d.issue(diagnostic_id::ext_unknown_escape, here).text(&text, 1);
        c.value = e;
        return c;
    }
}

/*
 * The value and type of a character-literal ([lex.ccon]/1-5), from its
 * spelling (without a ud-suffix).
 */
void lexer::convert_character_literal(const char *spelling, size_t length,
                                      token &out, literal_value &result)
{
    encoding_prefix prefix = out.preprocessing.prefix;
    source_location at = out.preprocessing.location;
    size_t i = 0;
    while (i < length && spelling[i] != '\'')
        i++;
    i++;                     /* the opening quote */
    size_t end = length - 1; /* the closing quote */

    /* [lex.ccon]/2-5: what one code unit of each encoding can hold */
    uint32_t largest;
    switch (prefix) {
    case encoding_prefix::u8:
        largest = 0x7F;
        break;
    case encoding_prefix::u:
        largest = 0xFFFF;
        break;
    case encoding_prefix::U:
        largest = 0x10FFFF;
        break;
    case encoding_prefix::L:
        largest = largest_code_unit(behavior, prefix);
        break;
    default:
        /* a single c-char "representable in the execution character set" is
           one UTF-8 code unit: U+0000 to U+007F */
        largest = 0x7F;
        break;
    }

    /* the last c-char's value, and the multicharacter value: the code
       units one after the other, the last the least significant byte */
    uint32_t last = 0;
    uint64_t multicharacter = 0;
    bool multicharacter_too_long = false;
    size_t count = 0;
    bool error = false;
    while (i < end) {
        c_char c = evaluate_c_char(spelling, end, &i, at, prefix);
        error |= c.error;
        if (c.invalid_encoding) {
            if (prefix == encoding_prefix::none)
                diagnostics.issue(diagnostic_id::warn_bad_character_encoding,
                                  at);
            else {
                diagnostics.issue(diagnostic_id::err_bad_character_encoding,
                                  at);
                error = true;
            }
        } else if (c.is_code_point && !c.error && c.value > largest) {
            /* [lex.ccon]/2: "If the value is not representable with a single
               UTF-8 code unit, the program is ill-formed", the same in /3 for
               UTF-16; for the others clang makes it an error too */
            diagnostics.issue(diagnostic_id::err_character_too_large, at);
            error = true;
        }
        last = c.value;
        multicharacter_too_long |= multicharacter >> 24 != 0;
        multicharacter = (multicharacter << 8 | (c.value & 0xFF)) & 0xFFFFFFFF;
        count++;
    }

    out.literal = literal_kind::character_literal;
    switch (prefix) {
    case encoding_prefix::u8:
        /* [lex.ccon]/2: "a character-literal of type char8_t"; before C++20
           (and with -fno-char8_t) char */
        if (behavior.char8_t_keyword)
            out.type = fundamental_type::char8_t_;
        else
            out.type = fundamental_type::char_;
        break;
    case encoding_prefix::u:
        out.type = fundamental_type::char16_t_;
        break;
    case encoding_prefix::U:
        out.type = fundamental_type::char32_t_;
        break;
    case encoding_prefix::L:
        out.type = fundamental_type::wchar_t_;
        break;
    default:
        if (count > 1)
            out.type = fundamental_type::int_;
        else
            out.type = fundamental_type::char_;
        break;
    }

    if (count > 1) {
        if (prefix == encoding_prefix::none) {
            /* [lex.ccon]/1: "A multicharacter literal ... is
               conditionally-supported, has type int, and has an
               implementation-defined value": the code units one after the
               other, the last the least significant byte, as GCC and clang */
            diagnostic_id multichar =
                diagnostic_id::warn_multichar_character_literal;
            if (count == 4)
                multichar = diagnostic_id::warn_four_char_character_literal;
            diagnostics.issue(multichar, at);
            if (multicharacter_too_long && !error)
                diagnostics.issue(diagnostic_id::warn_char_constant_too_large,
                                  at);
            result.value = (uint64_t)(int64_t)(int32_t)multicharacter;
            result.value_known = !error;
            return;
        }
        /* [lex.ccon]/2-4: "A UTF-8 character literal containing multiple
           c-chars is ill-formed", the same for UTF-16 and UTF-32; clang also
           for wide ones ([lex.ccon]/5 leaves them implementation-defined) */
        diagnostics.issue(diagnostic_id::err_multichar_character_literal, at)
            << (int)(prefix != encoding_prefix::L);
        error = true;
    }
    uint64_t value = last;
    /* [basic.fundamental]/7: a plain char that is signed sign-extends */
    if (out.type == fundamental_type::char_ && behavior.plain_char_is_signed &&
        (value & 0x80))
        value = (uint64_t)(int64_t)(int8_t)value;
    result.value = value;
    result.value_known = !error;
}

} // namespace cplusplus
