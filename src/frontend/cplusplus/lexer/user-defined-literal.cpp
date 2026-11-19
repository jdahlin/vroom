/*
 * user-defined-literal.cpp: [lex.ext] User-defined literals: the ud-suffix
 * after a literal.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * [lex.ext]:
 *
 *     user-defined-string-literal:
 *         string-literal ud-suffix
 *
 *     user-defined-character-literal:
 *         character-literal ud-suffix
 *
 *     ud-suffix:
 *         identifier
 *
 * The end of the ud-suffix after a character or string literal ending at
 * `at`, or `at` if there is none.  Before C++11 an identifier after a literal
 * is a preprocessing token of its own (clang warns that C++11 changes that).
 * [over.literal]/1: "Some literal suffix identifiers are reserved for future
 * standardization; see [usrlit.suffix].  A declaration whose
 * literal-operator-id uses such a literal suffix identifier is ill-formed, no
 * diagnostic required."  As a conforming extension clang treats a suffix that
 * does not start with an underscore (and is not one the library uses) as if
 * white space preceded it, after an error, so this does too; a suffix that
 * starts with a universal-character-name or an extended character is taken.
 */
uint32_t lexer::scan_ud_suffix(uint32_t at, preprocessing_token &token)
{
    translation_character t = peek(at);
    if (!t.extent)
        return at;
    uint32_t code_point = t.character;
    bool extended = false;
    if (t.character == '\\') {
        if (!scan_universal_character_name(at, &code_point, false))
            return at;
        extended = true;
    } else if (t.character >= 0x80) {
        if (!decode_utf8(file.physical_characters + at + t.extent - 1,
                         file.size - at, &code_point))
            return at;
        extended = true;
    }
    if (!is_identifier_start(code_point))
        return at;

    if (!behavior.has(language_feature::user_defined_literals)) {
        if (t.character == '_')
            diagnostics.issue(
                diagnostic_id::warn_cxx11_compat_user_defined_literal, at);
        else
            diagnostics.issue(
                diagnostic_id::warn_cxx11_compat_reserved_user_defined_literal,
                at);
        return at;
    }
    if (!extended && t.character != '_') {
        /* a string literal's suffix may be one of the library's: "s"
           ([basic.string.literals]) and "sv" ([string.view.literals]) among
           the ones numeric literals use; at most three characters */
        bool library = false;
        if (token.kind == preprocessing_token_kind::string_literal &&
            behavior.has(language_feature::library_ud_suffixes)) {
            char suffix[4];
            uint32_t length = 0, q = at;
            for (;;) {
                translation_character c = peek(q);
                if (!c.extent ||
                    !(is_nondigit(c.character) || is_digit(c.character) ||
                      (c.character == '$' && behavior.dollar_in_identifier))) {
                    library = is_valid_ud_suffix(behavior, suffix, length) ||
                              (length == 2 && !memcmp(suffix, "sv", 2));
                    break;
                }
                if (length == 3)
                    break;
                suffix[length++] = (char)c.character;
                q += c.extent;
            }
        }
        if (!library) {
            if (behavior.microsoft_compatibility)
                diagnostics.issue(
                    diagnostic_id::ext_ms_reserved_user_defined_literal, at);
            else
                diagnostics.issue(
                    diagnostic_id::ext_reserved_user_defined_literal, at);
            return at;
        }
    }
    bool differs = false;
    uint32_t end = scan_identifier(at, &differs);
    token.spelling_differs |= differs || t.extent > 1;
    return end;
}

} // namespace cplusplus
