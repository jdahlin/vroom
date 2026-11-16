/*
 * lex.ext.cpp: [lex.ext] User-defined literals: the ud-suffix after a
 * literal.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * [lex.ext]:
 *
 *     ud-suffix:
 *         identifier
 *
 * after a character or string literal (and in phase 7, after the digits of
 * a pp-number).  Returns the end of the ud-suffix starting at `at`, `at` if
 * there is none.  Before C++11 an identifier after a literal is a token of
 * its own.
 */
uint32_t lexer::scan_ud_suffix(uint32_t at, preprocessing_token &token)
{
    if (!behavior.at_least(standard_edition::cplusplus_11))
        return at;
    translation_character t = peek(at);
    uint32_t code_point = t.character;
    if (t.character == '\\' &&
        !scan_universal_character_name(at, &code_point, false))
        return at;
    if (t.character >= 0x80 &&
        !decode_utf8(file.physical_characters + at + t.extent - 1,
                     file.size - at, &code_point))
        return at;
    if (!t.extent || !is_identifier_start(code_point))
        return at;
    bool differs = false;
    uint32_t end = scan_identifier(at, &differs);
    token.spelling_differs |= differs || t.extent > 1;
    return end;
}

} // namespace cplusplus
