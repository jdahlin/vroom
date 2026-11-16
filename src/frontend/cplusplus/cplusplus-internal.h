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

/* ---- [lex.operators] --------------------------------------------------- */

VPRIV const char *
preprocessing_op_or_punc_clang_name(preprocessing_op_or_punc op);

/* ---- [lex.key] --------------------------------------------------------- */

/* the keyword table index of a spelling in this edition and mode, or -1 */
VPRIV int lookup_keyword(const implementation_defined_behavior &behavior,
                         const char *spelling, size_t length);
VPRIV const char *keyword_clang_name(int keyword);
/* [lex.key]/2: and, or, ...: the operator they stand for */
VPRIV bool keyword_is_alternative_representation(int keyword,
                                                 preprocessing_op_or_punc *op);

} // namespace cplusplus

#endif
