/*
 * lex.name.cpp: [lex.name] Identifiers.
 *
 *     identifier:
 *         identifier-nondigit
 *         identifier identifier-nondigit
 *         identifier digit
 *
 *     identifier-nondigit:
 *         nondigit
 *         universal-character-name
 *
 * [lex.name]/1 (n4861) allows the universal-character-names of Table 2 and
 * forbids those of Table 3 initially.  C++23 replaced both tables with
 * Unicode's XID_Start and XID_Continue (P1949R7, "C++ Identifier Syntax using
 * Unicode Standard Annex 31"), and clang applies that paper in every mode as
 * a defect report, so this implementation does too: lex.name.inc has the
 * two properties.  An extended character written in UTF-8 is, by
 * [lex.phases]/1.1, the universal-character-name that designates it.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

#include "lex.name.inc"

bool lexer::is_identifier_start(uint32_t code_point) const
{
    if (code_point < 0x80)
        return is_nondigit((unsigned char)code_point) ||
               (code_point == '$' && behavior.dollar_in_identifier);
    return code_point_in(xid_start_ranges,
                         sizeof xid_start_ranges / sizeof *xid_start_ranges,
                         code_point);
}

bool lexer::is_identifier_continue(uint32_t code_point) const
{
    if (code_point < 0x80)
        return is_nondigit((unsigned char)code_point) ||
               is_digit((unsigned char)code_point) ||
               (code_point == '$' && behavior.dollar_in_identifier);
    return code_point_in(
        xid_continue_ranges,
        sizeof xid_continue_ranges / sizeof *xid_continue_ranges, code_point);
}

/*
 * The longest identifier starting at `at` (whose first element the caller
 * has checked).  Each element is a nondigit or digit, '$', a
 * universal-character-name, or an extended character in UTF-8; the
 * identifier ends before the first character that is none of them.
 */
uint32_t lexer::scan_identifier(uint32_t at, bool *spelling_differs)
{
    const unsigned char *p = file.physical_characters;
    bool first = true;
    for (;;) {
        unsigned char c = p[at];
        /* the common case: a run of physical nondigits and digits */
        if (is_nondigit(c) || is_digit(c)) {
            at++;
            first = false;
            continue;
        }
        translation_character t = peek(at);
        if (!t.extent)
            return at;
        if (t.character == '$' && behavior.dollar_in_identifier) {
            /* [lex.name]/1 has no '$': clang allows it as an extension */
            diagnostics.issue(diagnostic_id::ext_dollar_in_identifier, at);
        } else if (t.character == '\\') {
            uint32_t code_point,
                n = scan_universal_character_name(at, &code_point, false);
            if (!n || !(first ? is_identifier_start(code_point)
                              : is_identifier_continue(code_point)))
                return at;
            /* [lex.charset]/2: "If a universal-character-name outside the
               c-char-sequence, s-char-sequence, or r-char-sequence of a
               character-literal or string-literal ... corresponds to a
               control character or to a character in the basic source
               character set, the program is ill-formed." */
            if (code_point < 0x80) {
                char c8 = (char)code_point;
                diagnostics.issue(diagnostic_id::err_ucn_escape_basic_scs, at)
                    .text(&c8, 1);
            }
            scan_universal_character_name(at, &code_point, true);
            *spelling_differs |= n != t.extent; /* a splice inside it */
            at += n;
            first = false;
            continue;
        } else if (t.character >= 0x80) {
            uint32_t code_point, n = decode_utf8(p + at + t.extent - 1,
                                                 file.size - at, &code_point);
            if (!n || !(first ? is_identifier_start(code_point)
                              : is_identifier_continue(code_point)))
                return at;
            *spelling_differs |= t.extent != 1;
            at += t.extent - 1 + n;
            first = false;
            continue;
        } else if (!is_nondigit(t.character) && !is_digit(t.character))
            return at;
        if (t.extent != 1) {
            *spelling_differs = true;
            read(at);
        }
        at += t.extent;
        first = false;
    }
}

} // namespace cplusplus
