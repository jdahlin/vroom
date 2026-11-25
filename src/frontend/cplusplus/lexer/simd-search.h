/*
 * simd-search.h: OPTIMIZATION: the lexer's searches over physical characters,
 * 16 (NEON, SSE2), 32 (AVX2) or 64 (AVX-512BW) at a time, after the old lexer's
 * (frontend/lex/lex.cpp: lex_comment_end_find, lex_line_end_find, ...).
 * Each returns the first position at or after `at` holding one of the bytes
 * it stops at; what to do there is left to the scalar code of the subclause
 * that calls it, so a search may stop early (a byte the scalar code only
 * passes over) but never late.  Every search stops at a NUL byte: a source
 * file is followed by 64 of them (source_file), and a load starts at or
 * before the first, so none goes past them; a NUL inside the file is the
 * scalar code's to look at.  The scalar versions are the definitions.
 */
#ifndef VCC_FRONTEND_CPLUSPLUS_LEXER_SIMD_SEARCH_H
#define VCC_FRONTEND_CPLUSPLUS_LEXER_SIMD_SEARCH_H

#include <stdint.h>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#define CPLUSPLUS_LEX_NEON 1
#elif defined(__AVX512BW__)
#include <immintrin.h>
#define CPLUSPLUS_LEX_AVX512 1
#elif defined(__AVX2__)
#include <immintrin.h>
#define CPLUSPLUS_LEX_AVX2 1
#elif defined(__SSE2__)
#include <emmintrin.h>
#define CPLUSPLUS_LEX_SSE2 1
#endif
#ifndef CPLUSPLUS_LEX_NEON
#define CPLUSPLUS_LEX_NEON 0
#endif
#ifndef CPLUSPLUS_LEX_AVX512
#define CPLUSPLUS_LEX_AVX512 0
#endif
#ifndef CPLUSPLUS_LEX_AVX2
#define CPLUSPLUS_LEX_AVX2 0
#endif
#ifndef CPLUSPLUS_LEX_SSE2
#define CPLUSPLUS_LEX_SSE2 0
#endif

namespace cplusplus
{

/* which bytes a search stops at */
enum class lex_stop : uint8_t {
    line_comment,  /* \n \r \ ? NUL: the end of a // comment, a splice,
                      a trigraph */
    block_comment, /* * / \ ? NUL */
    line_end,      /* \n \r NUL */
    identifier,    /* anything but A-Z a-z 0-9 _ */
    blank,         /* anything but a space or a horizontal tab */
};

inline bool lex_stops_at(lex_stop kind, unsigned char c)
{
    switch (kind) {
    case lex_stop::line_comment:
        return c == '\n' || c == '\r' || c == '\\' || c == '?' || !c;
    case lex_stop::block_comment:
        return c == '*' || c == '/' || c == '\\' || c == '?' || !c;
    case lex_stop::line_end:
        return c == '\n' || c == '\r' || !c;
    case lex_stop::identifier:
        return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_');
    case lex_stop::blank:
        return c != ' ' && c != '\t';
    }
    return true;
}

inline uint32_t lex_search_scalar(lex_stop kind, const unsigned char *p,
                                  uint32_t at)
{
    while (!lex_stops_at(kind, p[at]))
        at++;
    return at;
}

#if CPLUSPLUS_LEX_NEON
/* a byte mask (0xff where a byte stops) as 4 bits per byte */
inline uint64_t lex_neon_nibbles(uint8x16_t m)
{
    return vget_lane_u64(
        vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(m), 4)), 0);
}

inline uint8x16_t lex_neon_stops(lex_stop kind, uint8x16_t v)
{
    switch (kind) {
    case lex_stop::line_comment:
        return vorrq_u8(vorrq_u8(vceqq_u8(v, vdupq_n_u8('\n')),
                                 vceqq_u8(v, vdupq_n_u8('\r'))),
                        vorrq_u8(vorrq_u8(vceqq_u8(v, vdupq_n_u8('\\')),
                                          vceqq_u8(v, vdupq_n_u8('?'))),
                                 vceqzq_u8(v)));
    case lex_stop::block_comment:
        return vorrq_u8(vorrq_u8(vceqq_u8(v, vdupq_n_u8('*')),
                                 vceqq_u8(v, vdupq_n_u8('/'))),
                        vorrq_u8(vorrq_u8(vceqq_u8(v, vdupq_n_u8('\\')),
                                          vceqq_u8(v, vdupq_n_u8('?'))),
                                 vceqzq_u8(v)));
    case lex_stop::line_end:
        return vorrq_u8(vorrq_u8(vceqq_u8(v, vdupq_n_u8('\n')),
                                 vceqq_u8(v, vdupq_n_u8('\r'))),
                        vceqzq_u8(v));
    case lex_stop::identifier: {
        uint8x16_t lower = vorrq_u8(v, vdupq_n_u8(0x20));
        uint8x16_t letter =
            vcltq_u8(vsubq_u8(lower, vdupq_n_u8('a')), vdupq_n_u8(26));
        uint8x16_t digit =
            vcltq_u8(vsubq_u8(v, vdupq_n_u8('0')), vdupq_n_u8(10));
        uint8x16_t underscore = vceqq_u8(v, vdupq_n_u8('_'));
        return vmvnq_u8(vorrq_u8(vorrq_u8(letter, digit), underscore));
    }
    case lex_stop::blank:
        return vmvnq_u8(vorrq_u8(vceqq_u8(v, vdupq_n_u8(' ')),
                                 vceqq_u8(v, vdupq_n_u8('\t'))));
    }
    return vdupq_n_u8(0xff);
}

inline uint32_t lex_search(lex_stop kind, const unsigned char *p, uint32_t at)
{
    for (;; at += 16) {
        uint64_t m = lex_neon_nibbles(lex_neon_stops(kind, vld1q_u8(p + at)));
        if (m)
            return at + (uint32_t)(__builtin_ctzll(m) >> 2);
    }
}
#elif CPLUSPLUS_LEX_AVX512
/* the bytes a search stops at, one bit per byte: compares write masks */
inline uint64_t lex_avx512_stops(lex_stop kind, __m512i v)
{
    auto eq = [v](char c) {
        return _mm512_cmpeq_epi8_mask(v, _mm512_set1_epi8(c));
    };
    switch (kind) {
    case lex_stop::line_comment:
        return eq('\n') | eq('\r') | eq('\\') | eq('?') | eq(0);
    case lex_stop::block_comment:
        return eq('*') | eq('/') | eq('\\') | eq('?') | eq(0);
    case lex_stop::line_end:
        return eq('\n') | eq('\r') | eq(0);
    case lex_stop::identifier: {
        __m512i lower = _mm512_or_si512(v, _mm512_set1_epi8(0x20));
        uint64_t letter = _mm512_cmplt_epu8_mask(
            _mm512_sub_epi8(lower, _mm512_set1_epi8('a')),
            _mm512_set1_epi8(26));
        uint64_t digit = _mm512_cmplt_epu8_mask(
            _mm512_sub_epi8(v, _mm512_set1_epi8('0')), _mm512_set1_epi8(10));
        return ~(letter | digit | eq('_'));
    }
    case lex_stop::blank:
        return ~(eq(' ') | eq('\t'));
    }
    return ~(uint64_t)0;
}

inline uint32_t lex_search(lex_stop kind, const unsigned char *p, uint32_t at)
{
    for (;; at += 64) {
        uint64_t m = lex_avx512_stops(kind, _mm512_loadu_si512(p + at));
        if (m)
            return at + (uint32_t)__builtin_ctzll(m);
    }
}
#elif CPLUSPLUS_LEX_AVX2 || CPLUSPLUS_LEX_SSE2
#if CPLUSPLUS_LEX_AVX2
typedef __m256i lex_vector;
constexpr uint32_t lex_vector_bytes = 32;
#define LEX_SET1(c) _mm256_set1_epi8((char)(c))
#define LEX_EQ(a, b) _mm256_cmpeq_epi8(a, b)
#define LEX_OR(a, b) _mm256_or_si256(a, b)
#define LEX_SUB(a, b) _mm256_sub_epi8(a, b)
#define LEX_MIN(a, b) _mm256_min_epu8(a, b)
#define LEX_LOAD(p) _mm256_loadu_si256((const __m256i *)(p))
#define LEX_MASK(m) (uint32_t)_mm256_movemask_epi8(m)
#else
typedef __m128i lex_vector;
constexpr uint32_t lex_vector_bytes = 16;
#define LEX_SET1(c) _mm_set1_epi8((char)(c))
#define LEX_EQ(a, b) _mm_cmpeq_epi8(a, b)
#define LEX_OR(a, b) _mm_or_si128(a, b)
#define LEX_SUB(a, b) _mm_sub_epi8(a, b)
#define LEX_MIN(a, b) _mm_min_epu8(a, b)
#define LEX_LOAD(p) _mm_loadu_si128((const __m128i *)(p))
#define LEX_MASK(m) (uint32_t)_mm_movemask_epi8(m)
#endif

/* the bytes a search stops at, as a bit mask (identifier and blank: the
   bytes it does not stop at, inverted by the caller) */
inline uint32_t lex_x86_stops(lex_stop kind, lex_vector v)
{
    lex_vector zero = LEX_SET1(0);
    switch (kind) {
    case lex_stop::line_comment:
        return LEX_MASK(LEX_OR(
            LEX_OR(LEX_EQ(v, LEX_SET1('\n')), LEX_EQ(v, LEX_SET1('\r'))),
            LEX_OR(LEX_OR(LEX_EQ(v, LEX_SET1('\\')), LEX_EQ(v, LEX_SET1('?'))),
                   LEX_EQ(v, zero))));
    case lex_stop::block_comment:
        return LEX_MASK(LEX_OR(
            LEX_OR(LEX_EQ(v, LEX_SET1('*')), LEX_EQ(v, LEX_SET1('/'))),
            LEX_OR(LEX_OR(LEX_EQ(v, LEX_SET1('\\')), LEX_EQ(v, LEX_SET1('?'))),
                   LEX_EQ(v, zero))));
    case lex_stop::line_end:
        return LEX_MASK(
            LEX_OR(LEX_OR(LEX_EQ(v, LEX_SET1('\n')), LEX_EQ(v, LEX_SET1('\r'))),
                   LEX_EQ(v, zero)));
    case lex_stop::identifier: {
        lex_vector lower = LEX_OR(v, LEX_SET1(0x20));
        lex_vector letter = LEX_SUB(lower, LEX_SET1('a'));
        lex_vector digit = LEX_SUB(v, LEX_SET1('0'));
        /* Unsigned offsets in range are unchanged by clamping. */
        lex_vector letters = LEX_EQ(LEX_MIN(letter, LEX_SET1(25)), letter);
        lex_vector digits = LEX_EQ(LEX_MIN(digit, LEX_SET1(9)), digit);
        lex_vector in =
            LEX_OR(LEX_OR(letters, digits), LEX_EQ(v, LEX_SET1('_')));
        return ~LEX_MASK(in);
    }
    case lex_stop::blank:
        return ~LEX_MASK(
            LEX_OR(LEX_EQ(v, LEX_SET1(' ')), LEX_EQ(v, LEX_SET1('\t'))));
    }
    return ~(uint32_t)0;
}

inline uint32_t lex_search(lex_stop kind, const unsigned char *p, uint32_t at)
{
    for (;; at += lex_vector_bytes) {
        uint32_t m = lex_x86_stops(kind, LEX_LOAD(p + at));
        if (lex_vector_bytes == 16)
            m &= 0xffff;
        if (m)
            return at + (uint32_t)__builtin_ctz(m);
    }
}
#undef LEX_SET1
#undef LEX_EQ
#undef LEX_OR
#undef LEX_SUB
#undef LEX_MIN
#undef LEX_LOAD
#undef LEX_MASK
#else
inline uint32_t lex_search(lex_stop kind, const unsigned char *p, uint32_t at)
{
    return lex_search_scalar(kind, p, at);
}
#endif

} // namespace cplusplus

#endif
