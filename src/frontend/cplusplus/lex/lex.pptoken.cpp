/*
 * lex.pptoken.cpp: [lex.pptoken] Preprocessing tokens: translation phase 3.
 *
 *     preprocessing-token:
 *         header-name
 *         import-keyword
 *         module-keyword
 *         export-keyword
 *         identifier
 *         pp-number
 *         character-literal
 *         user-defined-character-literal
 *         string-literal
 *         user-defined-string-literal
 *         preprocessing-op-or-punc
 *         each non-white-space character that cannot be one of the above
 *
 * [lex.phases]/1.3: "The source file is decomposed into preprocessing tokens
 * and sequences of white-space characters (including comments)."  The
 * import-keyword, module-keyword and export-keyword are made by phase 4
 * ([lex.pptoken]/4), never here.
 */
#include <stdio.h>

#include "../cplusplus-internal.h"

namespace cplusplus
{

lexer::lexer(const source_file &f, const implementation_defined_behavior &b,
             diagnostic_consumer &d, lexer_options o)
    : file(f), behavior(b), diagnostics(d), options(o)
{
}

preprocessing_token lexer::next()
{
    return lex(false);
}
preprocessing_token lexer::next_allowing_header_name()
{
    return lex(true);
}

namespace
{

/* [lex.ccon]/1, [lex.string]: encoding-prefix: one of 'u8' 'u' 'U' 'L' */
struct literal_start {
    encoding_prefix prefix;
    bool raw;
    unsigned char quote; /* ' or " */
    uint32_t quote_at;   /* position of the quote */
};

} // namespace

/*
 * Whether a character or string literal starts at `at` with an
 * encoding-prefix or R: [lex.pptoken]/3.1, "If the next character begins a
 * sequence of characters that could be the prefix and initial double quote
 * of a raw string literal, such as R", the next preprocessing token shall be
 * a raw string literal."  The prefixes are C++11's (N2442), except L; u8
 * character literals are C++17's (N4267).
 */
static bool find_literal_start(const lexer &l, uint32_t at,
                               literal_start *start)
{
    char letters[4];
    uint32_t positions[4];
    uint32_t p = at;
    for (int i = 0; i < 4; i++) {
        lexer::translation_character t = l.peek(p);
        letters[i] = 0;
        positions[i] = p;
        if (t.extent) {
            letters[i] = (char)t.character;
            /* where the character itself is: after any splices before it
               (a quote is never a trigraph) */
            positions[i] = p + t.extent - 1;
        }
        p += t.extent;
    }
    bool unicode_and_raw =
        l.behavior.has(language_feature::unicode_and_raw_string_literals);
    int i = 0;
    encoding_prefix prefix = encoding_prefix::none;
    if (letters[0] == 'u' && letters[1] == '8' && unicode_and_raw)
        prefix = encoding_prefix::u8, i = 2;
    else if (letters[0] == 'u' && unicode_and_raw)
        prefix = encoding_prefix::u, i = 1;
    else if (letters[0] == 'U' && unicode_and_raw)
        prefix = encoding_prefix::U, i = 1;
    else if (letters[0] == 'L')
        prefix = encoding_prefix::L, i = 1;
    bool raw = false;
    if (letters[i] == 'R' && letters[i + 1] == '"' && unicode_and_raw)
        raw = true, i++;
    if (letters[i] != '"' && letters[i] != '\'')
        return false;
    if (i == 0)
        return false;
    if (letters[i] == '\'' && prefix == encoding_prefix::u8 &&
        !l.behavior.has(language_feature::utf8_character_literals))
        return false;
    *start = {prefix, raw, (unsigned char)letters[i], positions[i]};
    return true;
}

/* a character-literal (') or a string-literal (") by its quote */
static preprocessing_token_kind quoted_literal_kind(unsigned char quote)
{
    if (quote == '"')
        return preprocessing_token_kind::string_literal;
    return preprocessing_token_kind::character_literal;
}

/*
 * [lex.pptoken]/3: "If the input stream has been parsed into preprocessing
 * tokens up to a given character: ..."  The three cases are in the order the
 * paragraph gives them; the third, "the longest sequence of characters that
 * could constitute a preprocessing token", tries the categories in the order
 * in which a first character decides between them.
 */
preprocessing_token lexer::lex(bool header_name_permitted)
{
    preprocessing_token token;
    const unsigned char *p = file.physical_characters;

    for (;;) {
        uint32_t start = position;
        translation_character t = peek(start);
        token = preprocessing_token{};
        token.location = start;
        token.at_start_of_line = at_start_of_line;
        token.after_white_space = after_white_space;

        if (!t.extent || start >= file.size) {
            token.kind = preprocessing_token_kind::end_of_file;
            token.location = file.size;
            return token;
        }

        /* white space ([lex.pptoken]/2): new-lines are kept track of, since a
           directive is a line ([cpp.pre]/1) */
        if (is_white_space_character(t.character)) {
            /* as clang: a new-line starts a line, and only white space after
               the last new-line counts as leading white space */
            uint32_t end = start;
            bool new_line = false, space = after_white_space;
            for (;;) {
                translation_character w = peek(end);
                if (!w.extent || !is_white_space_character(w.character))
                    break;
                if (w.character == '\n' || w.character == '\r')
                    new_line = true, space = false;
                else
                    space = true;
                end += w.extent;
            }
            position = end;
            if (options.retain_white_space) {
                token.kind = preprocessing_token_kind::white_space;
                token.extent = end - start;
                at_start_of_line = new_line;
                after_white_space = false;
                return token;
            }
            at_start_of_line |= new_line;
            after_white_space = space;
            continue;
        }

        /* [lex.comment]: "Each comment is replaced by one space character"
           ([lex.phases]/1.3) */
        if (t.character == '/') {
            unsigned char second = peek(start + t.extent).character;
            if (second == '/' || second == '*') {
                position = scan_comment(start);
                after_white_space = true;
                /* a splice or trigraph inside: the spelling is the comment
                   after phases 1 and 2 */
                for (uint32_t q = start; q < position;) {
                    translation_character c = peek(q);
                    if (!c.extent)
                        break;
                    token.spelling_differs |= c.extent != 1;
                    q += c.extent;
                }
                if (options.retain_comments) {
                    token.kind = preprocessing_token_kind::comment;
                    token.extent = position - start;
                    at_start_of_line = false;
                    return token;
                }
                continue;
            }
        }
        if (t.character >= 0x80) {
            uint32_t code_point,
                n = decode_utf8(p + start + t.extent - 1, file.size - start,
                                &code_point);
            /* [ext.clang] clang_unicode_white_space */
            if (n && is_unicode_white_space(code_point) &&
                !options.preprocessed_input &&
                behavior.has(extension::clang_unicode_white_space)) {
                diagnostics.issue(diagnostic_id::ext_unicode_whitespace, start);
                position = start + t.extent - 1 + n;
                after_white_space = true;
                if (options.retain_white_space) {
                    /* a token of its own, as the character is no white space
                       of [lex.charset]/1 */
                    token.kind = preprocessing_token_kind::white_space;
                    token.extent = position - start;
                    at_start_of_line = false;
                    after_white_space = false;
                    return token;
                }
                continue;
            }
        }
        break;
    }

    uint32_t start = position, end = 0;
    translation_character t = peek(start);
    unsigned char c = t.character;
    bool differs = t.extent != 1;
    literal_start literal;
    uint32_t code_point = c;
    uint32_t ucn = 0;

    if (c == '\\')
        ucn = scan_universal_character_name(start, &code_point, false);
    else if (c >= 0x80 && !decode_utf8(p + start + t.extent - 1,
                                       file.size - start, &code_point))
        code_point = 0xFFFD + 1; /* not UTF-8: no identifier */

    if (header_name_permitted && (c == '<' || c == '"')) {
        /* [lex.pptoken]/3.3: a header-name "is only formed after the include
           or import preprocessing token in an #include or import directive,
           or within a has-include-expression" */
        bool ok;
        end = scan_header_name(start, &ok);
        if (ok)
            token.kind = preprocessing_token_kind::header_name;
        else
            end = 0;
    }
    if (end) {
    } else if ((c == 'u' || c == 'U' || c == 'L' || c == 'R') &&
               find_literal_start(*this, start, &literal)) {
        /* [lex.pptoken]/3.1, and the prefixed character and string literals */
        token.prefix = literal.prefix;
        for (uint32_t q = start; q < literal.quote_at;) {
            translation_character r = read(q);
            differs |= r.extent != 1;
            q += r.extent;
        }
        token.spelling_differs = differs;
        if (literal.raw) {
            bool ok;
            end = scan_raw_string(literal.quote_at, token, &ok);
            if (ok)
                token.kind = preprocessing_token_kind::string_literal;
            else {
                token.kind = preprocessing_token_kind::other_character;
                token.raw = false;
            }
        } else {
            token.kind = quoted_literal_kind(literal.quote);
            end = scan_character_or_string_literal(literal.quote_at, token);
        }
    } else if (c == '\'' || c == '"') {
        token.kind = quoted_literal_kind(c);
        end = scan_character_or_string_literal(start, token);
    } else if (is_identifier_start(code_point) &&
               (c != '\\' || (ucn && code_point >= 0xA0))) {
        token.kind = preprocessing_token_kind::identifier;
        end = scan_identifier(start, &differs);
        token.spelling_differs = differs;
    } else if (is_digit(c) ||
               (c == '.' && is_digit(peek(start + t.extent).character))) {
        token.kind = preprocessing_token_kind::pp_number;
        end = scan_pp_number(start, &differs);
        token.spelling_differs = differs;
    } else if (preprocessing_op_or_punc op;
               (end = scan_preprocessing_op_or_punc(start, &op,
                                                    &token.spelling_differs))) {
        token.kind = preprocessing_token_kind::preprocessing_op_or_punc;
        if (c == '?')
            diagnose_ignored_trigraph(start);
        token.op_or_punc = op;
    } else {
        /* "each non-white-space character that cannot be one of the above":
           an extended character is one character, the bytes of its UTF-8 */
        token.kind = preprocessing_token_kind::other_character;
        uint32_t n = 0;
        if (c >= 0x80)
            n = decode_utf8(p + start + t.extent - 1, file.size - start,
                            &code_point);
        end = start + t.extent;
        if (n)
            end += n - 1;
        if (c == '\\' && !ucn)
            scan_universal_character_name(start, &code_point, true);
        else if (c == '\\') {
            /* [lex.charset]/2: "If a universal-character-name outside the
               c-char-sequence, s-char-sequence, or r-char-sequence of a
               character-literal or string-literal ... corresponds to a
               control character or to a character in the basic source
               character set, the program is ill-formed." */
            end = start + ucn;
            scan_universal_character_name(start, &code_point, true);
            if (code_point >= 0x20 && code_point < 0x7F) {
                char basic = (char)code_point;
                diagnostics
                    .issue(diagnostic_id::err_ucn_escape_basic_scs, start)
                    .text(&basic, 1);
            } else if (code_point < 0xA0)
                diagnostics.issue(diagnostic_id::err_ucn_control_character,
                                  start);
        }
        /* clang does not diagnose it in phase 4's output */
        if (n && !options.preprocessed_input) {
            /* [lex.pptoken]/1: a character that is none of the forms makes
               the program ill-formed once it reaches phase 7; clang says
               why when it is next to an identifier or could only continue
               one */
            char hex[12];
            size_t length = format_code_point(code_point, hex);
            if (is_identifier_continue(code_point))
                diagnostics
                        .issue(
                            diagnostic_id::err_character_not_allowed_identifier,
                            start)
                        .text(hex, length)
                    << 1;
            else if (previous_identifier && previous_end == start)
                diagnostics
                        .issue(
                            diagnostic_id::err_character_not_allowed_identifier,
                            start)
                        .text(hex, length)
                    << 0;
            else
                diagnostics
                    .issue(diagnostic_id::err_character_not_allowed, start)
                    .text(hex, length);
        }
    }

    /* [lex.ext]: a user-defined-character-literal or -string-literal is the
       literal followed by a ud-suffix */
    if (token.kind == preprocessing_token_kind::character_literal ||
        token.kind == preprocessing_token_kind::string_literal) {
        uint32_t suffix_end = scan_ud_suffix(end, token);
        if (suffix_end != end) {
            token.ud_suffix_offset = end - start;
            if (token.kind == preprocessing_token_kind::character_literal)
                token.kind =
                    preprocessing_token_kind::user_defined_character_literal;
            else
                token.kind =
                    preprocessing_token_kind::user_defined_string_literal;
            end = suffix_end;
        }
    }

    token.location = start;
    token.extent = end - start;
    position = end;
    previous_end = end;
    previous_identifier = token.kind == preprocessing_token_kind::identifier;
    at_start_of_line = false;
    after_white_space = false;
    return token;
}

const char *clang_token_name(const lexer &l, const preprocessing_token &token)
{
    static constexpr const char *character_literals[] = {
        "char_constant", "utf8_char_constant", "utf16_char_constant",
        "utf32_char_constant", "wide_char_constant"};
    static constexpr const char *string_literals[] = {
        "string_literal", "utf8_string_literal", "utf16_string_literal",
        "utf32_string_literal", "wide_string_literal"};
    (void)l;
    switch (token.kind) {
    case preprocessing_token_kind::header_name:
        return "header_name";
    case preprocessing_token_kind::identifier:
        return "raw_identifier";
    case preprocessing_token_kind::pp_number:
        return "numeric_constant";
    case preprocessing_token_kind::character_literal:
    case preprocessing_token_kind::user_defined_character_literal:
        return character_literals[static_cast<int>(token.prefix)];
    case preprocessing_token_kind::string_literal:
    case preprocessing_token_kind::user_defined_string_literal:
        return string_literals[static_cast<int>(token.prefix)];
    case preprocessing_token_kind::preprocessing_op_or_punc:
        return preprocessing_op_or_punc_clang_name(token.op_or_punc);
    case preprocessing_token_kind::comment:
        return "comment";
    case preprocessing_token_kind::end_of_file:
        return "eof";
    default:
        return "unknown";
    }
}

} // namespace cplusplus
