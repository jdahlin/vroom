/*
 * identifier.cpp: [lex.name] Identifiers.
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
 * a defect report, so this implementation does too: identifier.inc has the
 * two properties.  An extended character written in UTF-8 is, by
 * [lex.phases]/1.1, the universal-character-name that designates it.
 */
#include <stdio.h>

#include "../cplusplus-internal.h"
#include "simd-search.h"

namespace cplusplus
{

#include "identifier.inc"

/*
 * UAX #31 section 7.1, the Mathematical Compatibility Notation Profile:
 * ID_Compat_Math_Start (partial differential, nabla, infinity and their bold
 * and italic forms) and ID_Compat_Math_Continue (superscript and subscript
 * digits and signs).  Not in [lex.name]; clang's extension, which only
 * changes programs that are ill-formed without it (a character of neither
 * XID property is a preprocessing token of its own, [lex.pptoken]/1).
 */
static constexpr code_point_range mathematical_notation_start_ranges[] = {
    {0x2202, 0x2202},   {0x2207, 0x2207},   {0x221E, 0x221E},
    {0x1D6C1, 0x1D6C1}, {0x1D6DB, 0x1D6DB}, {0x1D6FB, 0x1D6FB},
    {0x1D715, 0x1D715}, {0x1D735, 0x1D735}, {0x1D74F, 0x1D74F},
    {0x1D76F, 0x1D76F}, {0x1D789, 0x1D789}, {0x1D7A9, 0x1D7A9},
    {0x1D7C3, 0x1D7C3},
};
static constexpr code_point_range mathematical_notation_continue_ranges[] = {
    {0x00B2, 0x00B3}, {0x00B9, 0x00B9}, {0x2070, 0x2070},
    {0x2074, 0x207E}, {0x2080, 0x208E},
};

static bool is_mathematical_notation(uint32_t code_point, bool first)
{
    return code_point_in(mathematical_notation_start_ranges,
                         sizeof mathematical_notation_start_ranges /
                             sizeof *mathematical_notation_start_ranges,
                         code_point) ||
           (!first &&
            code_point_in(mathematical_notation_continue_ranges,
                          sizeof mathematical_notation_continue_ranges /
                              sizeof *mathematical_notation_continue_ranges,
                          code_point));
}

/*
 * Characters an identifier may contain that look like a punctuator or are
 * invisible (clang's list, Lexer.cpp): a warning, since the identifier is
 * well-formed but unlikely to be meant.  0: invisible.
 */
static constexpr struct homoglyph {
    uint32_t code_point;
    char looks_like;
} homoglyphs[] = {
    {0x00AD, 0},   {0x01C3, '!'}, {0x037E, ';'},  {0x200B, 0},    {0x200C, 0},
    {0x200D, 0},   {0x2060, 0},   {0x2061, 0},    {0x2062, 0},    {0x2063, 0},
    {0x2064, 0},   {0x2212, '-'}, {0x2215, '/'},  {0x2216, '\\'}, {0x2217, '*'},
    {0x2223, '|'}, {0x2227, '^'}, {0x2236, ':'},  {0x223C, '~'},  {0xA789, ':'},
    {0xFEFF, 0},   {0xFF01, '!'}, {0xFF03, '#'},  {0xFF04, '$'},  {0xFF05, '%'},
    {0xFF06, '&'}, {0xFF08, '('}, {0xFF09, ')'},  {0xFF0A, '*'},  {0xFF0B, '+'},
    {0xFF0C, ','}, {0xFF0D, '-'}, {0xFF0E, '.'},  {0xFF0F, '/'},  {0xFF1A, ':'},
    {0xFF1B, ';'}, {0xFF1C, '<'}, {0xFF1D, '='},  {0xFF1E, '>'},  {0xFF1F, '?'},
    {0xFF20, '@'}, {0xFF3B, '['}, {0xFF3C, '\\'}, {0xFF3D, ']'},  {0xFF3E, '^'},
    {0xFF5B, '{'}, {0xFF5C, '|'}, {0xFF5D, '}'},  {0xFF5E, '~'},
};

/* the code point as clang names it in a diagnostic: <U+XXXX> */
VPRIV size_t format_code_point(uint32_t code_point, char out[12])
{
    return (size_t)snprintf(out, 12, "%04X", code_point);
}

/*
 * An extended character (a universal-character-name, or written in UTF-8) in
 * an identifier, after the identifier's first element when `first` is
 * false: whether it is one, and the diagnostics of clang's extensions and
 * warnings for it.
 */
/* whether a code point may start an identifier (first) or continue one */
static bool identifier_character_allowed(const lexer &l, uint32_t code_point,
                                         bool first)
{
    if (first)
        return l.is_identifier_start(code_point);
    return l.is_identifier_continue(code_point);
}

/* whether a code point is XID_Start (first) or XID_Continue */
static bool in_xid_ranges(uint32_t code_point, bool first)
{
    if (first)
        return code_point_in(xid_start_ranges,
                             sizeof xid_start_ranges / sizeof *xid_start_ranges,
                             code_point);
    return code_point_in(
        xid_continue_ranges,
        sizeof xid_continue_ranges / sizeof *xid_continue_ranges, code_point);
}

bool lexer::accept_extended_identifier_character(uint32_t code_point,
                                                 bool first, source_location at,
                                                 bool written_in_utf8)
{
    char hex[12];
    if (identifier_character_allowed(*this, code_point, first)) {
        if (code_point >= 0x80 && !in_xid_ranges(code_point, first)) {
            size_t n = format_code_point(code_point, hex);
            diagnostics.issue(diagnostic_id::ext_mathematical_notation, at)
                .text(hex, n);
        }
        if (written_in_utf8)
            for (const homoglyph &h : homoglyphs)
                if (h.code_point == code_point) {
                    size_t n = format_code_point(code_point, hex);
                    if (h.looks_like)
                        diagnostics
                            .issue(diagnostic_id::warn_utf8_symbol_homoglyph,
                                   at)
                            .text(hex, n)
                            .text(&h.looks_like, 1);
                    else
                        diagnostics
                            .issue(diagnostic_id::warn_utf8_symbol_zero_width,
                                   at)
                            .text(hex, n);
                }
        return true;
    }
    return false;
}

bool lexer::is_identifier_start(uint32_t code_point) const
{
    if (code_point < 0x80)
        return is_nondigit((unsigned char)code_point) ||
               (code_point == '$' && behavior.dollar_in_identifier);
    if (is_mathematical_notation(code_point, true))
        return true;
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
    if (is_mathematical_notation(code_point, false))
        return true;
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
            /* OPTIMIZATION: the rest of the run (simd-search.h) */
            at = lex_search(lex_stop::identifier, p, at + 1);
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
            /* [lex.name]/1: "Each universal-character-name in an identifier
               shall designate a character whose encoding in ISO/IEC 10646
               falls into one of the ranges specified in Table 2", which
               starts at U+00A8: one naming a basic or control character is
               no identifier-nondigit, but a preprocessing token of its own
               ([lex.charset]/2 makes it ill-formed) */
            if (!n || code_point < 0xA0 ||
                !identifier_character_allowed(*this, code_point, first))
                return at;
            accept_extended_identifier_character(code_point, first, at, false);
            scan_universal_character_name(at, &code_point, true);
            *spelling_differs |= n != t.extent; /* a splice inside it */
            at += n;
            first = false;
            continue;
        } else if (t.character >= 0x80) {
            uint32_t code_point, n = decode_utf8(p + at + t.extent - 1,
                                                 file.size - at, &code_point);
            if (!n || !accept_extended_identifier_character(code_point, first,
                                                            at, true))
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
