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

/* ---- [lex.fcon] -------------------------------------------------------- */

VPRIV bool is_floating_point_literal_spelling(const char *spelling,
                                              size_t length);

} // namespace cplusplus

#endif
