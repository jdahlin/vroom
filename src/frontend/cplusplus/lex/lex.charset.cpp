/*
 * lex.charset.cpp: [lex.charset] Character sets: universal-character-names,
 * and UTF-8, the encoding in which this implementation reads physical source
 * files ([lex.phases]/1.1 leaves the mapping implementation-defined).
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

VPRIV bool code_point_in(const code_point_range *ranges, size_t count,
                         uint32_t code_point)
{
    size_t lo = 0, hi = count;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (code_point < ranges[mid].first)
            hi = mid;
        else if (code_point > ranges[mid].last)
            lo = mid + 1;
        else
            return true;
    }
    return false;
}

/* the well-formed UTF-8 sequences of Unicode Table 3-7: no overlong forms,
   no surrogates, nothing above U+10FFFF */
VPRIV uint32_t decode_utf8(const unsigned char *bytes, uint32_t available,
                           uint32_t *code_point)
{
    unsigned char b = bytes[0];
    uint32_t length, value;
    if (b < 0x80) {
        *code_point = b;
        return 1;
    }
    if (b >= 0xC2 && b <= 0xDF)
        length = 2, value = b & 0x1F;
    else if (b >= 0xE0 && b <= 0xEF)
        length = 3, value = b & 0x0F;
    else if (b >= 0xF0 && b <= 0xF4)
        length = 4, value = b & 0x07;
    else
        return 0;
    if (length > available)
        return 0;
    for (uint32_t i = 1; i < length; i++) {
        if ((bytes[i] & 0xC0) != 0x80)
            return 0;
        value = value << 6 | (bytes[i] & 0x3F);
    }
    static constexpr uint32_t minimum[] = {0, 0, 0x80, 0x800, 0x10000};
    if (value < minimum[length] || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF))
        return 0;
    *code_point = value;
    return length;
}

/*
 * [lex.charset]/2:
 *
 *     hex-quad:
 *         hexadecimal-digit hexadecimal-digit hexadecimal-digit hexadecimal-digit
 *
 *     universal-character-name:
 *         '\u' hex-quad
 *         '\U' hex-quad hex-quad
 *
 * plus, from C++23 (P2290R3, an extension before it in clang),
 * '\u{' simple-hexadecimal-digit-sequence '}'.
 *
 * `at` is the position of the backslash.  Returns the number of physical
 * characters of the universal-character-name and its code point, or 0 if
 * there is none, in which case the backslash is a preprocessing token of its
 * own (with `issue`, after a warning as clang gives).  The checks of an
 * identifier's universal-character-names ([lex.name]/1) are the caller's.
 */
uint32_t lexer::scan_universal_character_name(uint32_t at, uint32_t *code_point,
                                              bool issue)
{
    translation_character backslash = peek(at);
    uint32_t p = at + backslash.extent;
    translation_character kind = peek(p);
    if (backslash.character != '\\' ||
        (kind.character != 'u' && kind.character != 'U'))
        return 0;
    p += kind.extent;
    uint32_t digits = kind.character == 'u' ? 4 : 8, value = 0, found = 0;
    bool delimited = kind.character == 'u' && peek(p).character == '{';
    if (delimited)
        p += peek(p).extent;
    for (;;) {
        translation_character d = peek(p);
        if (!is_hexadecimal_digit(d.character) ||
            (!delimited && found == digits))
            break;
        unsigned v = is_digit(d.character) ? d.character - '0'
                                           : (d.character | 32) - 'a' + 10;
        if (value > 0x10FFFF)
            value = 0x110000; /* stays out of range */
        else
            value = value << 4 | v;
        found++;
        p += d.extent;
    }
    if (delimited) {
        translation_character close = peek(p);
        if (close.character != '}' || !found) {
            if (issue && !diagnostics.suppressed)
                diagnostics.issue(
                    found ? diagnostic_id::warn_delimited_ucn_incomplete
                          : diagnostic_id::warn_delimited_ucn_empty,
                    at)
                    << "u";
            return 0;
        }
        p += close.extent;
        if (issue &&
            !behavior.has(language_feature::delimited_escape_sequences))
            diagnostics.issue(diagnostic_id::ext_delimited_escape_sequence, at)
                << 0 << 1;
    } else if (found != digits) {
        if (issue && !diagnostics.suppressed) {
            if (!found) {
                char k = (char)kind.character;
                diagnostics.issue(diagnostic_id::warn_ucn_escape_no_digits, at)
                    .text(&k, 1);
            } else
                diagnostics.issue(diagnostic_id::warn_ucn_escape_incomplete,
                                  at);
        }
        return 0;
    }
    /* "The program is ill-formed if that number is not a code point or if it
       is a surrogate code point." */
    if (value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        if (issue)
            diagnostics.issue(diagnostic_id::err_ucn_escape_invalid, at);
    }
    *code_point = value;
    return p - at;
}

} // namespace cplusplus
