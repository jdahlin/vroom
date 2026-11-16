/*
 * lex.string.cpp: [lex.string] String literals and, since they end the same
 * way, [lex.ccon] Character literals, as preprocessing tokens: where each
 * ends.  Their values (phases 5 to 7) are not converted yet.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * [lex.ccon]:
 *
 *     character-literal:
 *         encoding-prefix_opt ''' c-char-sequence '''
 *
 *     c-char:
 *         any member of the basic source character set except the single-quote ''', backslash '\', or new-line character
 *         escape-sequence
 *         universal-character-name
 *
 * [lex.string]:
 *
 *     string-literal:
 *         encoding-prefix_opt '"' s-char-sequence_opt '"'
 *         encoding-prefix_opt 'R' raw-string
 *
 * For the end of the token an escape-sequence matters only in that '\'
 * escapes the next character: "\"" does not end at its second quote.  `at`
 * is the opening quote.  Without a closing quote before the end of the line,
 * [lex.pptoken]/2 makes the behavior undefined ("If a ' or a " character
 * matches the last category, the behavior is undefined"); clang warns and
 * makes the rest of the line one token of that last category, as here.
 */
uint32_t lexer::scan_character_or_string_literal(uint32_t at,
                                                 preprocessing_token &token)
{
    translation_character open = peek(at);
    unsigned char quote = open.character;
    uint32_t p = at + open.extent;
    for (;;) {
        translation_character t = peek(p);
        if (!t.extent || t.character == '\n' || t.character == '\r') {
            diagnostics.issue(diagnostic_id::ext_unterminated_char_or_string,
                              at)
                << (quote == '"' ? 1 : 0);
            token.kind = preprocessing_token_kind::other_character;
            return p;
        }
        if (t.extent != 1) {
            token.spelling_differs = true;
            read(p);
        } else if (t.character == '?')
            diagnose_ignored_trigraph(p);
        p += t.extent;
        if (t.character == quote)
            break;
        if (t.character == '\\') {
            translation_character e = peek(p);
            if (e.extent && e.character != '\n' && e.character != '\r') {
                if (e.extent != 1) {
                    token.spelling_differs = true;
                    read(p);
                }
                p += e.extent;
            }
        }
    }
    /* [lex.ccon]/7 and [lex.string] leave '' undefined; clang accepts it */
    if (quote == '\'' && p - at == 2)
        diagnostics.issue(diagnostic_id::ext_empty_character, at);
    return p;
}

/*
 * [lex.string]:
 *
 *     raw-string:
 *         '"' d-char-sequence_opt '(' r-char-sequence_opt ')' d-char-sequence_opt '"'
 *
 *     d-char:
 *         any member of the basic source character set except:
 *             space, the left parenthesis '(', the right parenthesis ')', the backslash '\', and the control characters
 *             representing horizontal tab, vertical tab, form feed, and newline.
 *
 * [lex.pptoken]/3.1: "Between the initial and final double quote characters
 * of the raw string, any transformations performed in phases 1 and 2
 * (universal-character-names and line splicing) are reverted": everything
 * after the opening quote is read as physical characters.  [lex.string]/1:
 * "A d-char-sequence shall consist of at most 16 characters."
 *
 * `quote` is the opening double quote.  Returns the end; *ok is false if
 * the delimiter is invalid, after a diagnostic.
 */
uint32_t lexer::scan_raw_string(uint32_t quote, preprocessing_token &token,
                                bool *ok)
{
    const unsigned char *p = file.physical_characters;
    uint32_t delimiter = quote + 1, length = 0;
    token.raw = true;
    diagnostics.issue(diagnostic_id::warn_cxx98_compat_raw_string_literal,
                      token.location);

    /* "d-char: any member of the basic source character set except: space,
       the left parenthesis (, the right parenthesis ), the backslash \, and
       the control characters representing horizontal tab, vertical tab, form
       feed, and newline."  C++26 adds $ @ ` (P2558R2), which clang takes in
       every edition with a warning before C++26. */
    for (;;) {
        unsigned char c = p[delimiter + length];
        bool d_char = c > ' ' && c < 0x7f && c != '(' && c != ')' && c != '\\';
        if (!d_char || length == 16)
            break;
        if (c == '$' || c == '@' || c == '`') {
            char text = (char)c;
            diagnostics
                .issue(
                    behavior.has(
                        language_feature::raw_string_delimiter_dollar_at_grave)
                        ? diagnostic_id::
                              warn_cxx26_compat_raw_string_literal_character_set
                        : diagnostic_id::
                              ext_cxx26_raw_string_literal_character_set,
                    delimiter + length)
                .text(&text, 1);
        }
        length++;
    }
    uint32_t prefix_end = delimiter + length;
    if (p[prefix_end] != '(' || prefix_end >= file.size) {
        /* [lex.string]/1: "A d-char-sequence shall consist of at most 16
           characters." */
        if (length == 16)
            diagnostics.issue(diagnostic_id::err_raw_delim_too_long,
                              prefix_end);
        else if (p[prefix_end] == '\n')
            diagnostics.issue(diagnostic_id::err_invalid_newline_raw_delim,
                              prefix_end);
        else {
            char text = (char)p[prefix_end];
            diagnostics
                .issue(diagnostic_id::err_invalid_char_raw_delim, prefix_end)
                .text(&text, 1);
        }
        /* clang's recovery: the token runs to the next double quote */
        uint32_t r = delimiter;
        while (r < file.size && p[r] != '"')
            r++;
        *ok = false;
        return r < file.size ? r + 1 : r;
    }
    /* "r-char: any member of the source character set, except a right
       parenthesis ) followed by the initial d-char-sequence (which may be
       empty) followed by a double quote ".": the first ) delimiter " ends
       the raw string */
    for (uint32_t r = prefix_end + 1; r < file.size; r++)
        if (p[r] == ')' && !memcmp(p + r + 1, p + delimiter, length) &&
            p[r + 1 + length] == '"') {
            *ok = true;
            return r + 2 + length;
        }
    /* [lex.phases]/1.3: "A source file shall not end in a partial
       preprocessing token" */
    diagnostics
        .issue(diagnostic_id::err_unterminated_raw_string, token.location)
        .text(reinterpret_cast<const char *>(p + delimiter), length);
    *ok = false;
    return file.size;
}

void lexer::append_code_unit(uint32_t unit)
{
    if (code_unit_count == code_unit_capacity) {
        code_unit_capacity = code_unit_capacity ? 2 * code_unit_capacity : 256;
        code_units = static_cast<uint32_t *>(
            realloc(code_units, code_unit_capacity * sizeof *code_units));
        if (!code_units)
            abort();
    }
    code_units[code_unit_count++] = unit;
}

/*
 * A code point in the execution character set of an encoding-prefix
 * ([lex.string]/5-10): "each successive element of the object representation
 * has the value of the corresponding code unit of the UTF-8 encoding" (also
 * for an ordinary string literal here), UTF-16 for u (with "surrogate pairs",
 * [lex.string]/8 Note 3), UTF-32 for U and a 32-bit wchar_t.
 */
void lexer::append_encoded(uint32_t code_point, encoding_prefix prefix)
{
    switch (prefix) {
    case encoding_prefix::none:
    case encoding_prefix::u8:
        if (code_point < 0x80)
            append_code_unit(code_point);
        else if (code_point < 0x800) {
            append_code_unit(0xC0 | code_point >> 6);
            append_code_unit(0x80 | (code_point & 0x3F));
        } else if (code_point < 0x10000) {
            append_code_unit(0xE0 | code_point >> 12);
            append_code_unit(0x80 | (code_point >> 6 & 0x3F));
            append_code_unit(0x80 | (code_point & 0x3F));
        } else {
            append_code_unit(0xF0 | code_point >> 18);
            append_code_unit(0x80 | (code_point >> 12 & 0x3F));
            append_code_unit(0x80 | (code_point >> 6 & 0x3F));
            append_code_unit(0x80 | (code_point & 0x3F));
        }
        break;
    case encoding_prefix::L:
        if (behavior.wchar_t_width >= 32) {
            append_code_unit(code_point);
            break;
        }
        [[fallthrough]];
    case encoding_prefix::u:
        if (code_point < 0x10000)
            append_code_unit(code_point);
        else {
            append_code_unit(0xD800 + ((code_point - 0x10000) >> 10));
            append_code_unit(0xDC00 + ((code_point - 0x10000) & 0x3FF));
        }
        break;
    case encoding_prefix::U:
        append_code_unit(code_point);
        break;
    }
}

/*
 * Phase 5 for one string-literal ([lex.phases]/1.5): its s-chars, or the
 * r-chars of a raw string literal, as code units of its encoding; and its
 * element type ([lex.string]/5-10).  The size of [lex.string]/13 is
 * string_length + 1 once phase 6 has concatenated it with its neighbours.
 */
void lexer::convert_string_literal(const char *spelling, size_t length,
                                   token &out)
{
    encoding_prefix prefix = out.preprocessing.prefix;
    source_location at = out.preprocessing.location;
    out.literal = literal_kind::string_literal;
    switch (prefix) {
    case encoding_prefix::u8:
        /* [lex.string]/6: "array of n const char8_t"; char before C++20
           ([diff.cpp17.lex]/5) */
        out.type = behavior.char8_t_keyword ? fundamental_type::char8_t_
                                            : fundamental_type::char_;
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
        out.type = fundamental_type::char_;
        break;
    }
    out.string_offset = (uint32_t)code_unit_count;

    size_t i = 0;
    while (i < length && spelling[i] != '"')
        i++;
    size_t end = length - 1; /* the closing quote */
    bool error = false;

    if (out.preprocessing.raw) {
        /* [lex.string]: raw-string: '"' d-char-sequence_opt '('
           r-char-sequence_opt ')' d-char-sequence_opt '"'.  [lex.string]/3
           Note 2: "A source-file new-line in a raw string literal results in
           a new-line in the resulting execution string literal." */
        size_t open = i + 1;
        while (open < length && spelling[open] != '(')
            open++;
        size_t delimiter = open - (i + 1);
        size_t close = end - delimiter - 1; /* the ')' */
        for (size_t r = open + 1; r < close;) {
            uint32_t code_point;
            uint32_t n = decode_utf8(
                reinterpret_cast<const unsigned char *>(spelling + r),
                (uint32_t)(close - r), &code_point);
            if (!n) {
                if (prefix == encoding_prefix::none)
                    diagnostics.issue(diagnostic_id::warn_bad_string_encoding,
                                      at);
                else {
                    diagnostics.issue(diagnostic_id::err_bad_string_encoding,
                                      at);
                    error = true;
                }
                append_code_unit((unsigned char)spelling[r++]);
                continue;
            }
            append_encoded(code_point, prefix);
            r += n;
        }
    } else {
        i++;
        while (i < end) {
            c_char c = evaluate_c_char(spelling, end, &i, at, prefix);
            error |= c.error;
            if (c.invalid_encoding) {
                if (prefix == encoding_prefix::none)
                    diagnostics.issue(diagnostic_id::warn_bad_string_encoding,
                                      at);
                else {
                    diagnostics.issue(diagnostic_id::err_bad_string_encoding,
                                      at);
                    error = true;
                }
                append_code_unit(c.value);
            } else if (c.is_code_point)
                append_encoded(c.value, prefix);
            else
                /* [lex.ccon]/6: "An escape sequence specifies a single
                   character", one code unit */
                append_code_unit(c.value);
        }
    }
    out.string_length = (uint32_t)(code_unit_count - out.string_offset);
    out.value_known = !error;
}

} // namespace cplusplus
