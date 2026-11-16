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
        }
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
    uint32_t delimiter = quote + 1, d = delimiter;
    token.raw = true;
    while (d < file.size && p[d] != '(') {
        unsigned char c = p[d];
        if (c == ' ' || c == ')' || c == '\\' || c == '\t' || c == '\v' ||
            c == '\f' || c == '\n' || c == '\r' || c == '"' || c < 0x20 ||
            c >= 0x7f) {
            if (d - delimiter > 16)
                diagnostics.issue(diagnostic_id::err_raw_delim_too_long, quote);
            else if (c == '\n' || c == '\r')
                diagnostics.issue(diagnostic_id::err_invalid_newline_raw_delim,
                                  d);
            else {
                char text[2] = {(char)c, 0};
                diagnostics.issue(diagnostic_id::err_invalid_char_raw_delim, d)
                    .text(text, 1);
            }
            *ok = false;
            return quote + 1;
        }
        d++;
    }
    uint32_t length = d - delimiter;
    if (length > 16) {
        diagnostics.issue(diagnostic_id::err_raw_delim_too_long, quote);
        *ok = false;
        return quote + 1;
    }
    if (d >= file.size) {
        diagnostics.issue(diagnostic_id::err_unterminated_raw_string, quote)
            .text(reinterpret_cast<const char *>(p + delimiter), length);
        *ok = false;
        return file.size;
    }
    /* the shortest r-char-sequence: the first ) delimiter " ends it */
    for (uint32_t r = d + 1; r < file.size; r++)
        if (p[r] == ')' && r + 1 + length < file.size + 1 &&
            !memcmp(p + r + 1, p + delimiter, length) &&
            p[r + 1 + length] == '"') {
            *ok = true;
            return r + 2 + length;
        }
    diagnostics.issue(diagnostic_id::err_unterminated_raw_string, quote)
        .text(reinterpret_cast<const char *>(p + delimiter), length);
    *ok = false;
    return file.size;
}

} // namespace cplusplus
