/*
 * cpp.stringize.cpp: [cpp.stringize] The # operator.
 *
 * [cpp.stringize]/2: "A character string literal is a string-literal with
 * no prefix.  If, in the replacement list, a parameter is immediately
 * preceded by a # preprocessing token, both are replaced by a single
 * character string literal preprocessing token that contains the spelling
 * of the preprocessing token sequence for the corresponding argument
 * (excluding placemarker tokens)."
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * [cpp.stringize]/2: "Each occurrence of white space between the
 * stringizing argument's preprocessing tokens becomes a single space
 * character in the character string literal.  White space before the first
 * preprocessing token and after the last preprocessing token comprising the
 * stringizing argument is deleted.  Otherwise, the original spelling of
 * each preprocessing token in the stringizing argument is retained in the
 * character string literal, except for special handling for producing the
 * spelling of string-literals and character-literals: a \ character is
 * inserted before each " and \ character of a character-literal or
 * string-literal (including the delimiting " characters). ... The character
 * string literal corresponding to an empty stringizing argument is ""."
 */
preprocessing_token preprocessor::stringize(const preprocessing_token *tokens,
                                            size_t count, source_location at)
{
    owned_buffer text, spelling;
    vbuffer_append(text.get(), "\"", 1);
    bool first = true;
    for (size_t i = 0; i < count; i++) {
        const preprocessing_token &t = tokens[i];
        if (t.kind == preprocessing_token_kind::placemarker)
            continue;
        if (!first && (t.after_white_space || t.at_start_of_line))
            vbuffer_append(text.get(), " ", 1);
        first = false;
        spelling.clear();
        files.spelling(t, spelling.get());
        bool literal =
            t.kind == preprocessing_token_kind::string_literal ||
            t.kind == preprocessing_token_kind::user_defined_string_literal ||
            t.kind == preprocessing_token_kind::character_literal ||
            t.kind == preprocessing_token_kind::user_defined_character_literal;
        for (size_t k = 0; k < spelling.size(); k++) {
            char c = spelling.data()[k];
            if (literal && (c == '"' || c == '\\'))
                vbuffer_append(text.get(), "\\", 1);
            /* [impl.clang] a new-line (inside a raw string literal) is written
               as an escape sequence */
            if (c == '\n') {
                vbuffer_append(text.get(), "\\n", 2);
                continue;
            }
            vbuffer_append(text.get(), &c, 1);
        }
    }
    /* "If the replacement that results is not a valid character string
       literal, the behavior is undefined": [impl.clang] clang drops a final \ that
       would escape the closing quote */
    size_t backslashes = 0;
    while (backslashes < text.size() - 1 &&
           text.data()[text.size() - 1 - backslashes] == '\\')
        backslashes++;
    if (backslashes % 2) {
        diagnostics.issue(diagnostic_id::pp_invalid_string_literal, at);
        text.get()->n--;
    }
    vbuffer_append(text.get(), "\"", 1);
    preprocessing_token result;
    spelled(text.data(), text.size(), &result);
    return result;
}

/* a preprocessing token phase 4 makes from its spelling, lexed from a
   piece of clang's "scratch space"; false unless the spelling is exactly
   one preprocessing token ([cpp.concat]/3) */
bool preprocessor::spelled(const char *text, size_t length,
                           preprocessing_token *out)
{
    source_files::entry &e =
        files.add_text("<scratch space>", text, length, true);
    diagnostics_suppressed quiet(diagnostics);
    *out = files.next(e);
    out->at_start_of_line = false;
    out->after_white_space = false;
    if (out->kind == preprocessing_token_kind::end_of_file ||
        out->location != e.base || out->extent != length)
        return false;
    return files.next(e).kind == preprocessing_token_kind::end_of_file;
}

} // namespace cplusplus
