/*
 * cplusplus-internal.h: what the files of the C++ front end share.
 *
 * The module is one TU by default (obj/unity/cplusplus.cpp #includes the
 * files), so a helper one file defines for another is VPRIV: static there,
 * extern when `make VCC_UNITY=` compiles the files one by one.
 */
#ifndef VCC_CPLUSPLUS_INTERNAL_H
#define VCC_CPLUSPLUS_INTERNAL_H

#include <stdlib.h>
#include <string.h>

#include "frontend/cplusplus/cplusplus.h"
#include "frontend/cplusplus/parse.h"

#ifdef VCC_UNITY
#define VPRIV static
#else
#define VPRIV
#endif

namespace cplusplus
{

/*
 * [lex.phases]/1.1-1.2 as one step: a character of a logical source line and
 * the number of physical source file characters it was made of: more than one
 * for a trigraph, or when splices come before it.
 */
struct lexer::translation_character {
    unsigned char character;
    uint32_t extent;
};

/* RAII: the lexer looks ahead without issuing diagnostics */
struct diagnostics_suppressed {
    diagnostic_consumer &consumer;
    explicit diagnostics_suppressed(diagnostic_consumer &c) : consumer(c)
    {
        consumer.suppressed++;
    }
    ~diagnostics_suppressed()
    {
        consumer.suppressed--;
    }
};

/* ---- [lex.ppnumber] --------------------------------------------------- */

/* a pp-number read by the grammars of [lex.icon] and [lex.fcon] */
struct lexer::pp_number_analysis {
    const char *spelling = nullptr;
    size_t length = 0;
    /* [lex.icon]/1 Table 7: the base N of the digits (10 for a decimal
       floating-point-literal, 16 for a hexadecimal one) */
    unsigned base = 10;
    /* the digits without the prefix: [digits_begin, suffix_begin) */
    size_t digits_begin = 0, suffix_begin = 0;
    bool period = false, exponent = false, floating = false;
    /* integer-suffix and floating-point-suffix, and clang's extensions */
    bool unsigned_suffix = false, long_suffix = false, long_long_suffix = false;
    bool size_suffix = false, float_suffix = false, float16_suffix = false;
    bool imaginary = false, bit_precise_suffix = false;
    uint8_t microsoft_integer_bits = 0;
    /* [lex.ext]: the suffix is a ud-suffix */
    bool ud_suffix = false;
    bool error = false;
};

VPRIV bool is_valid_ud_suffix(const implementation_defined_behavior &b,
                              const char *s, size_t n);

/* ---- [lex.ccon], [lex.string] ----------------------------------------- */

/* the meaning of one c-char or s-char: a code unit (an escape-sequence) or a
   code point the encoding turns into code units */
struct lexer::c_char {
    uint32_t value = 0;
    bool is_code_point = false;
    /* bytes that are not UTF-8: value is the byte */
    bool invalid_encoding = false;
    bool error = false;
};

VPRIV uint32_t largest_code_unit(const implementation_defined_behavior &b,
                                 encoding_prefix prefix);

/* ---- [lex.charset] ----------------------------------------------------- */

struct code_point_range {
    uint32_t first, last;
};

VPRIV bool code_point_in(const code_point_range *ranges, size_t count,
                         uint32_t code_point);
/* UTF-8, the encoding this implementation reads physical source files in
   ([lex.phases]/1.1): the length of the sequence at bytes (0: not UTF-8) */
VPRIV uint32_t decode_utf8(const unsigned char *bytes, uint32_t available,
                           uint32_t *code_point);

/* ---- [lex.name] -------------------------------------------------------- */

constexpr bool is_nondigit(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
constexpr bool is_digit(unsigned char c)
{
    return c >= '0' && c <= '9';
}
constexpr bool is_hexadecimal_digit(unsigned char c)
{
    return is_digit(c) || ((c | 32) >= 'a' && (c | 32) <= 'f');
}
/* [lex.charset]/1: space, horizontal tab, vertical tab, form feed, new-line */
constexpr bool is_white_space_character(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\n' ||
           c == '\r';
}

VPRIV uint32_t code_point_of_unicode_name(const char *name, size_t n);
VPRIV size_t format_code_point(uint32_t code_point, char out[12]);
/* clang's extension: white space other than [lex.charset]/1's (U+00A0 ...) */
VPRIV bool is_unicode_white_space(uint32_t code_point);

/* ---- [lex.operators] --------------------------------------------------- */

VPRIV const char *
preprocessing_op_or_punc_clang_name(preprocessing_op_or_punc op);

/* ---- [lex.key] --------------------------------------------------------- */

/* the keyword table index of a spelling in this edition and mode, or -1 */
VPRIV int lookup_keyword(const implementation_defined_behavior &behavior,
                         const char *spelling, size_t length);
VPRIV const char *keyword_clang_name(int keyword);
/* the keyword table: its size, and an entry's spelling */
VPRIV int keyword_table_size();
VPRIV const char *keyword_spelling(int keyword);
/* [lex.key]/2: and, or, ...: the operator they stand for */
VPRIV bool keyword_is_alternative_representation(int keyword,
                                                 preprocessing_op_or_punc *op);
/* phase 7 of an identifier whose keyword (or -1) is known */
VPRIV void identifier_token(token &out, literal_value *value, int keyword,
                            const char *spelling, size_t length);

/* ---- [cpp] ------------------------------------------------------------ */

inline bool is_operator(const preprocessing_token &t,
                        preprocessing_op_or_punc op)
{
    return t.kind == preprocessing_token_kind::preprocessing_op_or_punc &&
           preprocessing_op_or_punc_primary(t.op_or_punc) == op;
}
inline bool is_identifier(const preprocessing_token &t)
{
    return t.kind == preprocessing_token_kind::identifier;
}
/* the end of a directive's line, as preprocessor::directive_token gives it */
inline bool is_end(const preprocessing_token &t)
{
    return t.kind == preprocessing_token_kind::end_of_file;
}
/* where tokens[i] is, or `otherwise` when there are not that many */
inline source_location location_of(const preprocessing_token *tokens,
                                   size_t count, size_t i,
                                   source_location otherwise)
{
    if (i < count)
        return tokens[i].location;
    return otherwise;
}
inline bool is_string_literal(const preprocessing_token &t)
{
    return t.kind == preprocessing_token_kind::string_literal ||
           t.kind == preprocessing_token_kind::user_defined_string_literal;
}

/* whether a token is the identifier word */
VPRIV bool spelled_as(const source_files &files, const preprocessing_token &t,
                      const char *word);
VPRIV directive_kind find_directive(const implementation_defined_behavior &b,
                                    const char *spelling, size_t length);

/* a spelling's identifier, the hash the table keys on */
VPRIV uint32_t hash_spelling(const char *s, size_t n);

/* [cpp.cond]/6: __has_cpp_attribute's value of an attribute-token (0: none) */
VPRIV int64_t cpp_attribute_value(const implementation_defined_behavior &b,
                                  const char *scope, size_t scope_length,
                                  const char *name, size_t name_length);
/* a string literal of s's characters, " and \ escaped */
VPRIV void string_literal_of(const char *s, vbuffer *out);
/* [ext.clang]: clang's __has_feature (extension false) and __has_extension */
VPRIV bool has_feature(const implementation_defined_behavior &b,
                       const char *name, size_t length, bool extension);
VPRIV int64_t has_gnu_attribute(const implementation_defined_behavior &b,
                                const char *name, size_t length);
VPRIV int64_t
clang_cpp_attribute_value(const implementation_defined_behavior &b,
                          const char *operand, size_t length, bool c_attribute);
VPRIV int64_t clang_declspec_attribute_value(
    const implementation_defined_behavior &b, const char *name, size_t length);
VPRIV bool has_builtin(const implementation_defined_behavior &b,
                       const char *name, size_t length);

/* ---- parse ------------------------------------------------------------- */

/* [ext.gnu] the attribute-specifier-seq at n is GCC's __attribute__ alone */
VPRIV bool only_gnu_attributes(const syntax_tree &tree, node_index n);
/* ... has one of GCC's */
VPRIV bool has_gnu_attribute(const syntax_tree &tree, node_index n);

/* [dcl.type.simple]: the simple-type-specifiers that are one keyword */
VPRIV bool is_simple_type_keyword(standard_keyword k);

/* tokens [first, end) of a statement that did not parse use grammar a
   later step of doc/PLAN-cxx-parse.md writes (dcl.pre.cpp) */
VPRIV bool statement_uses_later_grammar(parser &p, token_index first,
                                        token_index end);

/* what is between the '(' just consumed and its ')' outside other
   brackets: how many ';' (an init-statement ends with one), and whether a
   ':' that is no conditional-expression's follows the last of them (a
   range-based for statement's, [stmt.iter]) */
struct parenthesized_parts {
    unsigned semicolons = 0;
    bool range_colon = false;
};
VPRIV parenthesized_parts scan_parenthesized(parser &p);

/* RAII: a declarative region of kind in declared_names for as long as the
   object lives; the region that was current is current again after it */
struct declarative_region {
    declared_names &names;
    scope_index enclosing;
    declarative_region(declared_names &names, scope_kind kind)
        : names(names), enclosing(names.current())
    {
        names.enter(kind);
    }
    ~declarative_region()
    {
        names.reenter(enclosing);
    }
    declarative_region(const declarative_region &) = delete;
    declarative_region &operator=(const declarative_region &) = delete;
};

/* RAII: a complete-class context ([class.mem]/6) for as long as the object
   lives: the class's members declared after it are found */
struct complete_class_context {
    declared_names &names;
    explicit complete_class_context(declared_names &names) : names(names)
    {
        names.complete_contexts++;
    }
    ~complete_class_context()
    {
        names.complete_contexts--;
    }
    complete_class_context(const complete_class_context &) = delete;
    complete_class_context &operator=(const complete_class_context &) = delete;
};

/* RAII: a flag of the parser set for as long as the object lives */
struct flag_set {
    bool &flag;
    bool was;
    flag_set(bool &flag, bool value) : flag(flag), was(flag)
    {
        flag = value;
    }
    ~flag_set()
    {
        flag = was;
    }
    flag_set(const flag_set &) = delete;
    flag_set &operator=(const flag_set &) = delete;
};

/* [lex.operators]: whether a token is the operator or punctuator op, in
   either of its spellings ([lex.digraph]) */
inline bool is_operator(const token &t, preprocessing_op_or_punc op)
{
    return t.kind == token_kind::operator_or_punctuator &&
           preprocessing_op_or_punc_primary(t.op_or_punc) == op;
}

/* OPTIMIZATION: the header cache's effects (cpp/cpp.header_cache.cpp) */
enum cache_effect_kind : uint8_t {
    effect_write,
    effect_source,
    effect_once,
    effect_guard,
    effect_entered,
    effect_child,
    effect_diagnostic_push,
    effect_diagnostic_pop,
    effect_diagnostic_map,
};

/* [lex.operators]: the bracket that closes the one opening at t, ( [ or {;
   count if t opens none */
inline preprocessing_op_or_punc closing_bracket(const token &t)
{
    if (is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return preprocessing_op_or_punc::RIGHT_PARENTHESIS;
    if (is_operator(t, preprocessing_op_or_punc::LEFT_BRACKET))
        return preprocessing_op_or_punc::RIGHT_BRACKET;
    if (is_operator(t, preprocessing_op_or_punc::LEFT_BRACE))
        return preprocessing_op_or_punc::RIGHT_BRACE;
    return preprocessing_op_or_punc::count;
}

/* whether t closes a bracket: ) ] or } */
inline bool is_closing_bracket(const token &t)
{
    return is_operator(t, preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
           is_operator(t, preprocessing_op_or_punc::RIGHT_BRACKET) ||
           is_operator(t, preprocessing_op_or_punc::RIGHT_BRACE);
}

/* [expr.ass]: assignment-operator, one of = *= /= %= += -= >>= <<= &= ^= |= */
inline bool is_assignment_operator(const token &t)
{
    if (t.kind != token_kind::operator_or_punctuator)
        return false;
    switch (preprocessing_op_or_punc_primary(t.op_or_punc)) {
    case preprocessing_op_or_punc::EQUAL:
    case preprocessing_op_or_punc::STAR_EQUAL:
    case preprocessing_op_or_punc::SLASH_EQUAL:
    case preprocessing_op_or_punc::PERCENT_EQUAL:
    case preprocessing_op_or_punc::PLUS_EQUAL:
    case preprocessing_op_or_punc::MINUS_EQUAL:
    case preprocessing_op_or_punc::GREATER_GREATER_EQUAL:
    case preprocessing_op_or_punc::LESS_LESS_EQUAL:
    case preprocessing_op_or_punc::AMPERSAND_EQUAL:
    case preprocessing_op_or_punc::CARET_EQUAL:
    case preprocessing_op_or_punc::BAR_EQUAL:
        return true;
    default:
        return false;
    }
}

} // namespace cplusplus

#endif
