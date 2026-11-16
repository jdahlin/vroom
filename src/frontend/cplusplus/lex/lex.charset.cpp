/*
 * lex.charset.cpp: [lex.charset] Character sets: universal-character-names,
 * and UTF-8, the encoding in which this implementation reads physical source
 * files ([lex.phases]/1.1 leaves the mapping implementation-defined).
 */
#include <stdio.h>

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
        (kind.character != 'u' && kind.character != 'U' &&
         kind.character != 'N'))
        return 0;
    if (kind.character == 'N') {
        /* P2071R2 named-universal-character: '\N{' n-char-sequence '}',
           C++23's, clang's extension before */
        uint32_t q = at + backslash.extent + kind.extent;
        translation_character open = peek(q);
        if (open.character != '{')
            return 0;
        q += open.extent;
        char name[128];
        size_t length = 0;
        for (;;) {
            translation_character t = peek(q);
            if (!t.extent || t.character == '\n' || t.character == '\r' ||
                length == sizeof name)
                return 0;
            q += t.extent;
            if (t.character == '}')
                break;
            name[length++] = (char)t.character;
        }
        uint32_t value = UINT32_MAX;
        if (length)
            value = code_point_of_unicode_name(name, length);
        if (issue) {
            if (value == UINT32_MAX)
                diagnostics.issue(diagnostic_id::err_invalid_ucn_name, at)
                    .text(name, length);
            else if (!behavior.has(
                         language_feature::named_universal_character_names))
                diagnostics.issue(diagnostic_id::ext_delimited_escape_sequence,
                                  at)
                    << 1 << 0;
        }
        if (value == UINT32_MAX)
            return 0;
        *code_point = value;
        return q - at;
    }
    p += kind.extent;
    uint32_t digits = 8, value = 0, found = 0;
    if (kind.character == 'u')
        digits = 4;
    bool delimited = kind.character == 'u' && peek(p).character == '{';
    if (delimited)
        p += peek(p).extent;
    for (;;) {
        translation_character d = peek(p);
        if (!is_hexadecimal_digit(d.character) ||
            (!delimited && found == digits))
            break;
        unsigned v = (unsigned)((d.character | 32) - 'a' + 10);
        if (is_digit(d.character))
            v = d.character - '0';
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
            diagnostic_id unfinished = diagnostic_id::warn_delimited_ucn_empty;
            if (found)
                unfinished = diagnostic_id::warn_delimited_ucn_incomplete;
            if (issue && !diagnostics.suppressed)
                diagnostics.issue(unfinished, at) << "u";
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

/*
 * The characters of Unicode's White_Space property beyond [lex.charset]/1's
 * five (and U+180E, White_Space before Unicode 6.3).  [lex.pptoken]/2's white
 * space is only "space, horizontal tab, new-line, vertical tab, and
 * form-feed", so U+00A0 is a preprocessing token of the last category and
 * the program is ill-formed ([lex.pptoken]/1); clang, and this
 * implementation, take it as white space after a warning, an extension
 * ([intro.compliance]/8) that changes only ill-formed programs.
 */
VPRIV bool is_unicode_white_space(uint32_t code_point)
{
    static constexpr code_point_range white_space[] = {
        {0x0085, 0x0085}, {0x00A0, 0x00A0}, {0x1680, 0x1680},
        {0x180E, 0x180E}, {0x2000, 0x200A}, {0x2028, 0x2029},
        {0x202F, 0x202F}, {0x205F, 0x205F}, {0x3000, 0x3000},
    };
    return code_point_in(white_space, sizeof white_space / sizeof *white_space,
                         code_point);
}

#include "lex.charset.names.inc"

namespace
{

int base64_digit(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    return 63;
}

/* one entry of unicode_names at `at`, decoded onto the previous name in
   `name` (*length its length); returns the next entry's offset */
size_t decode_name_entry(size_t at, char *name, size_t *length,
                         uint32_t *code_point)
{
    size_t shared = (size_t)base64_digit(unicode_names[at++]);
    *length = shared;
    while (unicode_names[at] != ';')
        name[(*length)++] = unicode_names[at++];
    at++;
    uint32_t cp = 0;
    for (int k = 0; k < 4; k++)
        cp = cp << 6 | (uint32_t)base64_digit(unicode_names[at++]);
    *code_point = cp;
    return at;
}

int compare_names(const char *a, size_t an, const char *b, size_t bn)
{
    size_t common = an;
    if (bn < common)
        common = bn;
    int c = memcmp(a, b, common);
    if (c)
        return c;
    if (an < bn)
        return -1;
    if (an > bn)
        return 1;
    return 0;
}

/* The Unicode Standard 3.12, "Conjoining Jamo Behavior": the names of the
   Hangul syllables U+AC00 to U+D7A3 are made of their jamo's short names */
constexpr const char *hangul_l[] = {"G", "GG", "N", "D",  "DD", "R", "M",
                                    "B", "BB", "S", "SS", "",   "J", "JJ",
                                    "C", "K",  "T", "P",  "H"};
constexpr const char *hangul_v[] = {"A",   "AE", "YA", "YAE", "EO", "E",  "YEO",
                                    "YE",  "O",  "WA", "WAE", "OE", "YO", "U",
                                    "WEO", "WE", "WI", "YU",  "EU", "YI", "I"};
constexpr const char *hangul_t[] = {"",   "G",  "GG", "GS", "N",  "NJ", "NH",
                                    "D",  "L",  "LG", "LM", "LB", "LS", "LT",
                                    "LP", "LH", "M",  "B",  "BS", "S",  "SS",
                                    "NG", "J",  "C",  "K",  "T",  "P",  "H"};

} // namespace

/*
 * P2071R2 (C++23), named-universal-character:
 *
 *     named-universal-character:
 *         '\N{' n-char-sequence '}'
 *
 * "designates the character" whose Unicode name, or name alias, is the
 * n-char-sequence: matched exactly, as clang matches it, with every kind of
 * alias of NameAliases.txt (control, correction, alternate, figment,
 * abbreviation).  Returns the code point, or UINT32_MAX for none.
 */
VPRIV uint32_t code_point_of_unicode_name(const char *name, size_t n)
{
    static constexpr char hangul[] = "HANGUL SYLLABLE ";
    if (n > sizeof hangul - 1 && !memcmp(name, hangul, sizeof hangul - 1)) {
        const char *rest = name + sizeof hangul - 1;
        size_t rest_length = n - (sizeof hangul - 1);
        for (uint32_t s = 0; s < 19 * 21 * 28; s++) {
            char syllable[16];
            int k = snprintf(syllable, sizeof syllable, "%s%s%s",
                             hangul_l[s / (21 * 28)],
                             hangul_v[s % (21 * 28) / 28], hangul_t[s % 28]);
            if ((size_t)k == rest_length &&
                !memcmp(syllable, rest, rest_length))
                return 0xAC00 + s;
        }
        return UINT32_MAX;
    }
    for (const named_range &r : unicode_named_ranges) {
        size_t p = strlen(r.prefix);
        if (n < p + 5 || memcmp(name, r.prefix, p) || name[p] != '-')
            continue;
        uint32_t cp = 0;
        size_t digits = n - p - 1;
        bool hex = digits == 4 || digits == 5;
        for (size_t k = p + 1; hex && k < n; k++) {
            char c = name[k];
            hex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
            uint32_t v = (uint32_t)(c - 'A' + 10);
            if (c <= '9')
                v = (uint32_t)(c - '0');
            cp = cp << 4 | v;
        }
        /* the name is spelled with exactly four digits below U+10000 */
        unsigned expected = 4;
        if (cp > 0xFFFF)
            expected = 5;
        if (hex && cp >= r.first && cp <= r.last && digits == expected)
            return cp;
    }

    /* the last block whose first name is not after the name */
    char current[128];
    size_t length;
    uint32_t cp;
    size_t blocks =
        sizeof unicode_name_block_offsets / sizeof *unicode_name_block_offsets;
    size_t lo = 0, hi = blocks;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        decode_name_entry(unicode_name_block_offsets[mid], current, &length,
                          &cp);
        if (compare_names(current, length, name, n) <= 0)
            lo = mid;
        else
            hi = mid;
    }
    size_t at = unicode_name_block_offsets[lo];
    for (size_t k = 0; k < UNICODE_NAME_BLOCK &&
                       lo * UNICODE_NAME_BLOCK + k < UNICODE_NAME_COUNT;
         k++) {
        at = decode_name_entry(at, current, &length, &cp);
        int c = compare_names(current, length, name, n);
        if (!c)
            return cp;
        if (c > 0)
            break;
    }
    return UINT32_MAX;
}

} // namespace cplusplus
