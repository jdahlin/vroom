/*
 * vcc lexer, see lex.h.
 */
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif
#if defined(__x86_64__) && !defined(VLEX_SCALAR)
#define LEX_X86_SIMD 1
#include "port/x86native.h"
#endif
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#ifdef __APPLE__
#include <pthread/qos.h>
#endif
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/intern.h"
#include "frontend/lex/lex.h"

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

static const char *const lex_token_kind_names[LEX_TOKEN_KIND_COUNT] = {
	"eof", "raw_identifier", "numeric_constant",
	"char_constant", "wide_char_constant", "utf8_char_constant",
	"utf16_char_constant", "utf32_char_constant",
	"string_literal", "wide_string_literal", "utf8_string_literal",
	"utf16_string_literal", "utf32_string_literal",
	"unknown",
	"l_square", "r_square", "l_paren", "r_paren", "l_brace", "r_brace",
	"period", "ellipsis", "amp", "ampamp", "ampequal", "star", "starequal",
	"plus", "plusplus", "plusequal", "minus", "arrow", "minusminus",
	"minusequal", "tilde", "exclaim", "exclaimequal", "slash", "slashequal",
	"percent", "percentequal", "less", "lessless", "lessequal",
	"lesslessequal", "greater", "greatergreater", "greaterequal",
	"greatergreaterequal", "caret", "caretequal", "pipe", "pipepipe",
	"pipeequal", "question", "colon", "coloncolon", "semi", "equal",
	"equalequal", "comma", "hash", "hashhash",
};

const char *lex_token_name(int kind)
{
	return kind >= 0 && kind < LEX_TOKEN_KIND_COUNT ? lex_token_kind_names[kind] : "?";
}

/* ---- pass 1: SIMD classification into bitmasks ---------------------- */

/*
 * Byte classes by nibble lookup: class = lo[c & 15] & hi[c >> 4].
 *   bit 0  hi 4,6  lo 1-f   A-O a-o
 *   bit 1  hi 5,7  lo 0-a   P-Z p-z
 *   bit 2  hi 3    lo 0-9   0-9
 *   bit 3  hi 5    lo f     _
 *   bit 4  hi 2    lo 4,e   $ .  ('.' is in the ID mask, and in P: see fast)
 *   bit 5  hi 2    lo 0     space
 *   bit 6  hi 0    lo 9,b,c tab vt ff
 *   bit 7  hi 0    lo a,d   newline, carriage return
 */
#ifdef LEX_X86_SIMD
#define LEX_TABLE __attribute__((visibility("hidden")))
#else
#define LEX_TABLE static
#endif
LEX_TABLE const uint8_t lex_low_nibble_table[16] = {
	0x26, 0x07, 0x07, 0x07, 0x17, 0x07, 0x07, 0x07,
	0x07, 0x47, 0x83, 0x41, 0x41, 0x81, 0x11, 0x09,
};
LEX_TABLE const uint8_t lex_high_nibble_table[16] = {
	0xc0, 0x00, 0x30, 0x04, 0x01, 0x0a, 0x01, 0x02,
};

/*
 * Structural tables, indexed by byte (punctuation is ASCII):
 *   ttab[c]  bit 0  may double (&& || ++ -- ## :: << >>)
 *            bit 1  may take '=' (&= |= += -= *= ^= == != <= >= %= /=)
 *            bit 2  may take '>' (-> :> %>)
 *            bit 3  may take ':' or '%' (<: <%)
 *            bit 4  punctuator or '.'
 *            bit 6  '/', '%' or '.': with '/', '*', ':', '%' or '.' left
 *                   to the handlers (comments, digraphs, "..", and the
 *                   rare "%/", "/:", "./", "/." too)
 *            bit 7  needs a handler: quotes, '\\', control, '@', '`'
 *   gtab[c]  bit 1 '=', bit 2 '>', bit 3 ':' or '%', bit 6 '/', '*', ':',
 *            '%' or '.', bit 7 '+' or '-'
 * Masks whose property is in bit 7 go to lex_mask_pack() as they are.
 */
LEX_TABLE uint8_t lex_structure_table[128], lex_group_table[64];
static uint8_t lex_class_table[128];	/* cltab: the byte classes above */

static void lex_structure_tables_init(void)
{
	for (int c = 0; c < 128; c++) {
		uint8_t t = 0;

		if (c && strchr("&|+-#:<>", c))
			t |= 1;
		if (c && strchr("&|+-*^=!<>%/", c))
			t |= 2;
		if (c && strchr("-:%", c))
			t |= 4;
		if (c == '<')
			t |= 8;
		if (c && strchr("[](){}~?;,&|+-*^=!#:<>%/.", c))
			t |= 16;
		if (c == '"' || c == '\'' || c == '\\' || c == '@' || c == '`' || c == 0x7f ||
		    (c < 0x20 && !strchr(" \t\v\f\n\r", c)) || c == 0)
			t |= 128;
		if (c == '/' || c == '%' || c == '.')
			t |= 64;
		lex_structure_table[c] = t;
	}
	for (int c = 0; c < 128; c++)
		lex_class_table[c] = lex_low_nibble_table[c & 15] & lex_high_nibble_table[c >> 4];
	lex_group_table['='] = 2;
	lex_group_table['>'] = 4;
	lex_group_table[':'] = lex_group_table['%'] = 8 | 64;
	lex_group_table['/'] = lex_group_table['*'] = lex_group_table['.'] = 64;
	lex_group_table['+'] = lex_group_table['-'] = 128;
}

#include "masks.h"

#ifdef __ARM_NEON
/*
 * 64 byte masks to a 64-bit mask, for vectors loaded with vld4q (only bit
 * 7 of each byte counts):
 * a[k] lane j is byte 4j + k, which the shift-insert chain puts at bit
 * 4j + k.  Five instructions instead of the eight of a pairwise-add tree.
 */
static inline uint64_t lex_mask_pack(uint8x16_t a0, uint8x16_t a1, uint8x16_t a2, uint8x16_t a3)
{
	uint8x16_t t0 = vsriq_n_u8(a1, a0, 1), t1 = vsriq_n_u8(a3, a2, 1);
	uint8x16_t t2 = vsriq_n_u8(t1, t0, 2), t3 = vsriq_n_u8(t2, t2, 4);

	return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(t3), 4)), 0);
}

/*
 * Per 64-byte block, bit i of each mask describes byte i:
 *   ID    identifier character or '.'  WS   whitespace incl. newlines
 *   NL    \n or \r                   DIG  digit
 *   P     punctuator character or '.'
 *   PAIR  bytes i, i+1 form a two-character punctuator
 *   X     the structural pass stops here: literal, comment, splice, UCN,
 *         non-ASCII, NUL, stray character
 *   NS    the next byte is + or - (maybe a sign inside a pp-number)
 * Blocks [from, to) of the file at s.
 */
static void pp_paste_result_classify(const uint8_t *s, size_t from, size_t to, uint64_t *m[LEX_MASK_COUNT])
{
	const uint8x16_t mid = vdupq_n_u8(0x1f), mdg = vdupq_n_u8(0x04);
	const uint8x16_t mws = vdupq_n_u8(0xe0);
	const uint8x16_t k64 = vdupq_n_u8(64);
	const uint8x16_t b16 = vdupq_n_u8(16);
	const uint8x16_t pairb = vdupq_n_u8(0x0e);

	for (size_t b = from; b < to; b++) {
		const uint8_t *q = s + 64 * b;
		const uint8x16x4_t v = vld4q_u8(q);
		uint8x16_t vn[4], t[4], gn[4], tg[4], c[4], r[4];

		/* the next byte of each lane */
		vn[0] = v.val[1], vn[1] = v.val[2], vn[2] = v.val[3];
		vn[3] = vextq_u8(v.val[0], vld1q_dup_u8(q + 64), 1);
		{
			/*
			 * The table registers are loaded per block (5 loads)
			 * instead of being live across the loop: that spilled
			 * and rematerialized ~60 instructions per block.
			 */
			const uint8_t *tp = lex_structure_table, *gp = lex_group_table, *cp = lex_class_table;
			uint8x16_t hv[4];

			__asm__("" : "+r"(tp), "+r"(gp), "+r"(cp));
			for (int k = 0; k < 4; k++)
				hv[k] = vsubq_u8(v.val[k], k64);	/* 64..127 -> 0..63 */
			const uint8x16x4_t t0 = vld1q_u8_x4(tp), t1 = vld1q_u8_x4(tp + 64);

			for (int k = 0; k < 4; k++)
				t[k] = vqtbx4q_u8(vqtbl4q_u8(t0, v.val[k]), t1, hv[k]);
			const uint8x16x4_t c0 = vld1q_u8_x4(cp), c1 = vld1q_u8_x4(cp + 64);

			for (int k = 0; k < 4; k++)
				c[k] = vqtbx4q_u8(vqtbl4q_u8(c0, v.val[k]), c1, hv[k]);
			const uint8x16x4_t g = vld1q_u8_x4(gp);

			for (int k = 0; k < 4; k++)
				gn[k] = vqtbl4q_u8(g, vn[k]), tg[k] = vandq_u8(t[k], gn[k]);
		}
#define MASK(j, expr) do {						\
		for (int k = 0; k < 4; k++)					\
			r[k] = (expr);						\
		m[j][b] = lex_mask_pack(r[0], r[1], r[2], r[3]);				\
	} while (0)
		MASK(LEX_MASK_PREFIX, vtstq_u8(t[k], b16));
		/* bit 7: a pair by the tables, or a doubling character doubled (ttab bit 0 shifted up) */
		MASK(LEX_MASK_PAIR, vorrq_u8(vtstq_u8(tg[k], pairb), vshlq_n_u8(vandq_u8(t[k], vceqq_u8(v.val[k], vn[k])), 7)));
		/* bit 7: handler, non-ASCII, comment or digraph */
		MASK(LEX_MASK_HANDLER, vorrq_u8(vorrq_u8(t[k], v.val[k]), vshlq_n_u8(tg[k], 1)));
		MASK(LEX_MASK_IDENTIFIER, vtstq_u8(c[k], mid));
		MASK(LEX_MASK_WHITESPACE, vtstq_u8(c[k], mws));
		MASK(LEX_MASK_NEWLINE, c[k]);
		MASK(LEX_MASK_DIGIT, vtstq_u8(c[k], mdg));
		MASK(LEX_MASK_SIGN_NEXT, gn[k]);
#undef MASK
	}
}

/* ---- NEON searches -------------------------------------------------- */

/* 4 bits per byte -> 64-bit mask; returns the byte index of the first hit */
static inline uint64_t lex_nibbles_first_index(uint8x16_t eq)
{
	return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq), 4)), 0);
}

/* first position >= p holding '*' before '/', a backslash or NUL */
static inline size_t lex_comment_end_find(const char *s, size_t p)
{
	const uint8x16_t star = vdupq_n_u8('*'), sl = vdupq_n_u8('/'), bs = vdupq_n_u8('\\');

	for (;; p += 16) {
		uint8x16_t v = vld1q_u8((const uint8_t *)s + p), w = vld1q_u8((const uint8_t *)s + p + 1);
		uint64_t m = lex_nibbles_first_index(vorrq_u8(vandq_u8(vceqq_u8(v, star), vceqq_u8(w, sl)),
					      vorrq_u8(vceqq_u8(v, bs), vceqzq_u8(v))));

		if (m)
			return p + (__builtin_ctzll(m) >> 2);
	}
}

/* first position >= p holding '\n', '\r' or NUL */
static inline size_t lex_line_end_find(const char *s, size_t p)
{
	const uint8x16_t nl = vdupq_n_u8('\n'), cr = vdupq_n_u8('\r');

	for (;; p += 16) {
		uint8x16_t v = vld1q_u8((const uint8_t *)s + p);
		uint64_t m = lex_nibbles_first_index(vorrq_u8(vorrq_u8(vceqq_u8(v, nl), vceqq_u8(v, cr)), vceqzq_u8(v)));

		if (m)
			return p + (__builtin_ctzll(m) >> 2);
	}
}

/* first position >= p holding q, a backslash or a byte <= '\r' */
static inline size_t lex_quote_find(const char *s, size_t p, uint8_t q)
{
	const uint8x16_t vq = vdupq_n_u8(q), bs = vdupq_n_u8('\\'), cr = vdupq_n_u8('\r');

	for (;; p += 16) {
		uint8x16_t v = vld1q_u8((const uint8_t *)s + p);
		uint8x16_t hit = vorrq_u8(vorrq_u8(vceqq_u8(v, vq), vceqq_u8(v, bs)), vcleq_u8(v, cr));
		uint64_t m = lex_nibbles_first_index(hit);

		if (m)
			return p + (__builtin_ctzll(m) >> 2);
	}
}

#else /* !__ARM_NEON */

#ifdef LEX_X86_SIMD
#define pp_paste_result_classify classify_scalar
#define lex_comment_end_find find_cend_scalar
#define lex_line_end_find find_nl_scalar
#define lex_quote_find find_quote_scalar
#endif

/* Portable fallback; also independent searches for the scalar oracle. */

static void pp_paste_result_classify(const uint8_t *s, size_t from, size_t to, uint64_t *m[LEX_MASK_COUNT])
{
	for (size_t b = from; b < to; b++) {
		const uint8_t *q = s + 64 * b;
		uint64_t mp = 0, mpair = 0, mx = 0, mid = 0, mws = 0, mnl = 0, mdg = 0, mns = 0;

		for (int i = 0; i < 64; i++) {
			unsigned x = q[i], y = q[i + 1];
			unsigned t = x < 128 ? lex_structure_table[x] : 0, c = x < 128 ? lex_class_table[x] : 0;
			unsigned gn = y < 64 ? lex_group_table[y] : 0, tg = t & gn;
			uint64_t bit = (uint64_t)1 << i;

			if (t & 16)
				mp |= bit;
			if ((tg & 0x0e) || ((t & 1) && x == y))
				mpair |= bit;
			if ((t | x) & 128 || (tg & 64))
				mx |= bit;
			if (c & 0x1f)
				mid |= bit;
			if (c & 0xe0)
				mws |= bit;
			if (c & 0x80)
				mnl |= bit;
			if (c & 0x04)
				mdg |= bit;
			if (gn & 128)
				mns |= bit;
		}
		m[LEX_MASK_PREFIX][b] = mp, m[LEX_MASK_PAIR][b] = mpair, m[LEX_MASK_HANDLER][b] = mx, m[LEX_MASK_IDENTIFIER][b] = mid;
		m[LEX_MASK_WHITESPACE][b] = mws, m[LEX_MASK_NEWLINE][b] = mnl, m[LEX_MASK_DIGIT][b] = mdg, m[LEX_MASK_SIGN_NEXT][b] = mns;
	}
}

/* first position >= p holding '*' before '/', a backslash or NUL */
static inline size_t lex_comment_end_find(const char *s, size_t p)
{
	for (;; p++)
		if ((s[p] == '*' && s[p + 1] == '/') || s[p] == '\\' || !s[p])
			return p;
}

/* first position >= p holding '\n', '\r' or NUL */
static inline size_t lex_line_end_find(const char *s, size_t p)
{
	for (;; p++)
		if (s[p] == '\n' || s[p] == '\r' || !s[p])
			return p;
}

/* first position >= p holding q, a backslash or a byte <= '\r' */
static inline size_t lex_quote_find(const char *s, size_t p, uint8_t q)
{
	for (;; p++) {
		uint8_t ch = (uint8_t)s[p];

		if (ch == q || ch == '\\' || ch <= '\r')
			return p;
	}
}
#ifdef LEX_X86_SIMD
#undef pp_paste_result_classify
#undef lex_comment_end_find
#undef lex_line_end_find
#undef lex_quote_find
static void (*pp_paste_result_classify)(const uint8_t *, size_t, size_t, uint64_t **) = classify_scalar;
static size_t (*lex_comment_end_find)(const char *, size_t) = find_cend_scalar;
static size_t (*lex_line_end_find)(const char *, size_t) = find_nl_scalar;
static size_t (*lex_quote_find)(const char *, size_t, uint8_t) = find_quote_scalar;

void lex_bytes_classify_avx512(const uint8_t *, size_t, size_t, uint64_t **);
void lex_bytes_classify_avx2(const uint8_t *, size_t, size_t, uint64_t **);
size_t lex_comment_end_find_avx2(const char *, size_t);
size_t lex_line_end_find_avx2(const char *, size_t);
size_t lex_quote_find_avx2(const char *, size_t, uint8_t);

/* Called under lex_tables_once, before the lexer publishes its dispatch table.
 * The compiler's CPU detector checks OSXSAVE/XCR0 as well as CPUID, so a
 * CPU feature is usable only when the OS preserves the required registers.
 * VCC_LEX_SIMD caps the choice for testing; it cannot enable unsupported ISA. */
static void lex_simd_init_x86(void)
{
	const char *cap = getenv("VCC_LEX_SIMD");
	x86_features features, disabled;
	const char *cpu;
	if (cap && !strcmp(cap, "scalar")) return;
	if (!x86_native_features(&features, &disabled, &cpu) || !x86_feature_has(features, X86_FEATURE_AVX2)) return;
	pp_paste_result_classify = lex_bytes_classify_avx2;
	lex_comment_end_find = lex_comment_end_find_avx2;
	lex_line_end_find = lex_line_end_find_avx2;
	lex_quote_find = lex_quote_find_avx2;
	if (cap && !strcmp(cap, "avx2")) return;
	if (x86_feature_has(features, X86_FEATURE_AVX512F) && x86_feature_has(features, X86_FEATURE_AVX512BW) &&
	    x86_feature_has(features, X86_FEATURE_AVX512VBMI))
		pp_paste_result_classify = lex_bytes_classify_avx512;
}
#endif
#endif /* host SIMD */

/* ---- pass 2: tokens ------------------------------------------------- */

enum {
	LEX_CLASS_OTHER, LEX_CLASS_WHITESPACE, LEX_CLASS_NEWLINE, LEX_CLASS_CARRIAGE_RETURN, LEX_CLASS_NUL, LEX_CLASS_IDENTIFIER, LEX_CLASS_LITERAL_PREFIX, LEX_CLASS_DIGIT, LEX_CLASS_PUNCTUATOR,
	LEX_CLASS_SINGLE_QUOTE, LEX_CLASS_DOUBLE_QUOTE, LEX_CLASS_BACKSLASH, LEX_CLASS_HIGH_BIT,
};

static uint8_t lex_byte_class_table[256];
static uint8_t lex_identifier_byte_table[256];

/* fast path classes and punctuator table */
enum { LEX_FAST_IDENTIFIER, LEX_FAST_NUMBER, LEX_FAST_PUNCTUATOR, LEX_FAST_SLOW };
enum { LEX_PUNCTUATOR_ALTERNATIVE = 1 << 11, LEX_PUNCTUATOR_SLOW = 1 << 12 };
static uint8_t lex_fast_class_table[256];
static uint8_t lex_punctuator_row_table[256];		/* row in ptab, 0 = not a punctuator */
static uint16_t lex_punctuator_table[27][256];		/* kind | len << 8 | PT_* */
static struct { uint8_t c, kind, len; } lex_punctuator_alternative_table[LEX_TOKEN_KIND_COUNT];
static uint8_t lex_punctuator_kind_table[1 << 16];		/* punctuator kind by its first two bytes (LE) */

static void lex_tables_init(void)
{
#ifdef LEX_X86_SIMD
	lex_simd_init_x86();
#endif
	for (int c = 0; c < 256; c++) {
		uint8_t k = LEX_CLASS_OTHER;

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$')
			k = LEX_CLASS_IDENTIFIER;
		if (c == 'L' || c == 'u' || c == 'U' || c == 'R')
			k = LEX_CLASS_LITERAL_PREFIX;
		if (c >= '0' && c <= '9')
			k = LEX_CLASS_DIGIT;
		if (c >= 0x80)
			k = LEX_CLASS_HIGH_BIT;
		if (c && strchr("[](){}.&*+-~!/%<>^|?:;=,#", c))
			k = LEX_CLASS_PUNCTUATOR;
		lex_identifier_byte_table[c] = k == LEX_CLASS_IDENTIFIER || k == LEX_CLASS_LITERAL_PREFIX || k == LEX_CLASS_DIGIT;
		lex_byte_class_table[c] = k;
	}
	lex_byte_class_table[' '] = lex_byte_class_table['\t'] = lex_byte_class_table['\v'] = lex_byte_class_table['\f'] = LEX_CLASS_WHITESPACE;
	lex_byte_class_table['\n'] = LEX_CLASS_NEWLINE;
	lex_byte_class_table['\r'] = LEX_CLASS_CARRIAGE_RETURN;
	lex_byte_class_table[0] = LEX_CLASS_NUL;
	lex_byte_class_table['\''] = LEX_CLASS_SINGLE_QUOTE;
	lex_byte_class_table['"'] = LEX_CLASS_DOUBLE_QUOTE;
	lex_byte_class_table['\\'] = LEX_CLASS_BACKSLASH;

	static const char pch[] = "[](){}~?;,.&|+-*^=!#:<>%/";
	static const struct { char a, b; uint8_t kind; } two[] = {
		{ '&', '&', LEX_TOKEN_AMPERSAND_AMPERSAND }, { '&', '=', LEX_TOKEN_AMPERSAND_EQUAL }, { '|', '|', LEX_TOKEN_PIPE_PIPE },
		{ '|', '=', LEX_TOKEN_PIPE_EQUAL }, { '+', '+', LEX_TOKEN_PLUS_PLUS }, { '+', '=', LEX_TOKEN_PLUS_EQUAL },
		{ '-', '>', LEX_TOKEN_ARROW }, { '-', '-', LEX_TOKEN_MINUS_MINUS }, { '-', '=', LEX_TOKEN_MINUS_EQUAL },
		{ '*', '=', LEX_TOKEN_STAR_EQUAL }, { '^', '=', LEX_TOKEN_CARET_EQUAL }, { '=', '=', LEX_TOKEN_EQUAL_EQUAL },
		{ '!', '=', LEX_TOKEN_EXCLAMATION_EQUAL }, { '#', '#', LEX_TOKEN_HASH_HASH }, { ':', ':', LEX_TOKEN_COLON_COLON },
		{ ':', '>', LEX_TOKEN_RIGHT_SQUARE }, { '<', '<', LEX_TOKEN_LESS_LESS }, { '<', '=', LEX_TOKEN_LESS_EQUAL },
		{ '<', ':', LEX_TOKEN_LEFT_SQUARE }, { '<', '%', LEX_TOKEN_LEFT_BRACE }, { '>', '>', LEX_TOKEN_GREATER_GREATER },
		{ '>', '=', LEX_TOKEN_GREATER_EQUAL }, { '%', '=', LEX_TOKEN_PERCENT_EQUAL }, { '%', '>', LEX_TOKEN_RIGHT_BRACE },
		{ '/', '=', LEX_TOKEN_SLASH_EQUAL },
	};
	static const uint8_t one[] = {
		LEX_TOKEN_LEFT_SQUARE, LEX_TOKEN_RIGHT_SQUARE, LEX_TOKEN_LEFT_PAREN, LEX_TOKEN_RIGHT_PAREN, LEX_TOKEN_LEFT_BRACE, LEX_TOKEN_RIGHT_BRACE, LEX_TOKEN_TILDE,
		LEX_TOKEN_QUESTION, LEX_TOKEN_SEMICOLON, LEX_TOKEN_COMMA, LEX_TOKEN_PERIOD, LEX_TOKEN_AMPERSAND, LEX_TOKEN_PIPE, LEX_TOKEN_PLUS, LEX_TOKEN_MINUS,
		LEX_TOKEN_STAR, LEX_TOKEN_CARET, LEX_TOKEN_EQUAL, LEX_TOKEN_EXCLAMATION, LEX_TOKEN_HASH, LEX_TOKEN_COLON, LEX_TOKEN_LESS, LEX_TOKEN_GREATER,
		LEX_TOKEN_PERCENT, LEX_TOKEN_SLASH,
	};

	for (int c = 0; c < 256; c++)
		lex_fast_class_table[c] = LEX_FAST_SLOW;
	for (int c = 0; c < 256; c++)
		if (lex_identifier_byte_table[c])
			lex_fast_class_table[c] = c >= '0' && c <= '9' ? LEX_FAST_NUMBER : LEX_FAST_IDENTIFIER;
	for (int i = 0; pch[i]; i++) {
		uint8_t c = (uint8_t)pch[i];

		lex_fast_class_table[c] = LEX_FAST_PUNCTUATOR;
		lex_punctuator_row_table[c] = (uint8_t)(i + 1);
		for (int d = 0; d < 256; d++)
			lex_punctuator_table[i + 1][d] = (uint16_t)(one[i] | 1 << 8);
		lex_punctuator_table[i + 1]['\\'] |= LEX_PUNCTUATOR_SLOW;	/* maybe a splice */
	}
	for (unsigned i = 0; i < sizeof(two) / sizeof(two[0]); i++)
		lex_punctuator_table[lex_punctuator_row_table[(uint8_t)two[i].a]][(uint8_t)two[i].b] = (uint16_t)(two[i].kind | 2 << 8);
	/* three-character forms: decided by the third character */
	lex_punctuator_table[lex_punctuator_row_table['<']]['<'] |= LEX_PUNCTUATOR_ALTERNATIVE;
	lex_punctuator_alternative_table[LEX_TOKEN_LESS_LESS].c = '=', lex_punctuator_alternative_table[LEX_TOKEN_LESS_LESS].kind = LEX_TOKEN_LESS_LESS_EQUAL, lex_punctuator_alternative_table[LEX_TOKEN_LESS_LESS].len = 3;
	lex_punctuator_table[lex_punctuator_row_table['>']]['>'] |= LEX_PUNCTUATOR_ALTERNATIVE;
	lex_punctuator_alternative_table[LEX_TOKEN_GREATER_GREATER].c = '=', lex_punctuator_alternative_table[LEX_TOKEN_GREATER_GREATER].kind = LEX_TOKEN_GREATER_GREATER_EQUAL,
		lex_punctuator_alternative_table[LEX_TOKEN_GREATER_GREATER].len = 3;
	lex_punctuator_table[lex_punctuator_row_table['.']]['.'] |= LEX_PUNCTUATOR_ALTERNATIVE;		/* ".." is '.' unless "..." */
	lex_punctuator_alternative_table[LEX_TOKEN_PERIOD].c = '.', lex_punctuator_alternative_table[LEX_TOKEN_PERIOD].kind = LEX_TOKEN_ELLIPSIS, lex_punctuator_alternative_table[LEX_TOKEN_PERIOD].len = 3;
	/* to the handlers: comments, ".5", "%:" (maybe "%:%:") */
	lex_punctuator_table[lex_punctuator_row_table['/']]['*'] |= LEX_PUNCTUATOR_SLOW;
	lex_punctuator_table[lex_punctuator_row_table['/']]['/'] |= LEX_PUNCTUATOR_SLOW;
	lex_punctuator_table[lex_punctuator_row_table['%']][':'] |= LEX_PUNCTUATOR_SLOW;
	for (int d = '0'; d <= '9'; d++)
		lex_punctuator_table[lex_punctuator_row_table['.']][d] |= LEX_PUNCTUATOR_SLOW;
	for (int c = 0; c < 256; c++)
		for (int d = 0; d < 256; d++)
			lex_punctuator_kind_table[c | d << 8] = (uint8_t)(lex_punctuator_table[lex_punctuator_row_table[c]][d] & 0xff);
	/*
	 * The structural loop takes every token's kind from ktab: a token
	 * starting with an identifier character is an identifier or (digit) a
	 * number, and ".5" a number (".." always goes to the handlers).
	 */
	for (int c = 0; c < 256; c++)
		for (int d = 0; d < 256; d++) {
			if (lex_identifier_byte_table[c])
				lex_punctuator_kind_table[c | d << 8] = c >= '0' && c <= '9' ? LEX_TOKEN_NUMBER : LEX_TOKEN_IDENTIFIER;
			if (c == '.' && d >= '0' && d <= '9')
				lex_punctuator_kind_table[c | d << 8] = LEX_TOKEN_NUMBER;
		}
}

/* length of the line splice ("\\", horizontal space, newline) at p, or 0 */
static inline size_t lex_splice_length(const char *s, size_t p)
{
	size_t q = p + 1;

	if (s[p] != '\\')
		return 0;
	while (s[q] == ' ' || s[q] == '\t' || s[q] == '\v' || s[q] == '\f')
		q++;
	if (s[q] != '\n' && s[q] != '\r')
		return 0;
	/* \r\n and \n\r are one newline */
	return q + 1 + ((s[q + 1] == '\n' || s[q + 1] == '\r') && s[q + 1] != s[q]) - p;
}

static inline size_t lex_splices_skip(const char *s, size_t p)
{
	size_t n;

	while ((n = lex_splice_length(s, p)))
		p += n;
	return p;
}

/* position of the logical character after the one at p */
static inline size_t lex_character_next(const char *s, size_t p)
{
	p++;
	if (unlikely(s[p] == '\\'))
		p = lex_splices_skip(s, p);
	return p;
}

static size_t lex_utf8_length(const uint8_t *s);

/* C11 annex D: characters allowed in identifiers */
static bool lex_character_is_c11_identifier(uint32_t c, bool first)
{
	static const uint32_t r[][2] = {
		{ 0xa8, 0xa8 }, { 0xaa, 0xaa }, { 0xad, 0xad }, { 0xaf, 0xaf },
		{ 0xb2, 0xb5 }, { 0xb7, 0xba }, { 0xbc, 0xbe }, { 0xc0, 0xd6 },
		{ 0xd8, 0xf6 }, { 0xf8, 0xff }, { 0x100, 0x167f }, { 0x1681, 0x180d },
		{ 0x180f, 0x1fff }, { 0x200b, 0x200d }, { 0x202a, 0x202e },
		{ 0x203f, 0x2040 }, { 0x2054, 0x2054 }, { 0x2060, 0x206f },
		{ 0x2070, 0x218f }, { 0x2460, 0x24ff }, { 0x2776, 0x2793 },
		{ 0x2c00, 0x2dff }, { 0x2e80, 0x2fff }, { 0x3004, 0x3007 },
		{ 0x3021, 0x302f }, { 0x3031, 0x303f }, { 0x3040, 0xd7ff },
		{ 0xf900, 0xfd3d }, { 0xfd40, 0xfdcf }, { 0xfdf0, 0xfe44 },
		{ 0xfe47, 0xfffd },
	};

	if (first && ((c >= 0x300 && c <= 0x36f) || (c >= 0x1dc0 && c <= 0x1dff) ||
		      (c >= 0x20d0 && c <= 0x20ff) || (c >= 0xfe20 && c <= 0xfe2f)))
		return false;
	if (c >= 0x10000)
		return c <= 0xeffff && (c & 0xffff) <= 0xfffd;
	for (unsigned i = 0; i < sizeof(r) / sizeof(r[0]); i++)
		if (c >= r[i][0] && c <= r[i][1])
			return true;
	return false;
}

static inline int lex_hex_digit_value(uint8_t c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	c |= 0x20;
	return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/*
 * Length of an identifier character outside ASCII at p: a UCN (\uXXXX,
 * \UXXXXXXXX) or a UTF-8 sequence for an allowed code point; else 0.
 */
static size_t lex_identifier_extended_length(const char *s, size_t p, bool first)
{
	const uint8_t *u = (const uint8_t *)s + p;
	uint32_t c = 0;
	size_t n;

	if (u[0] == '\\') {
		n = u[1] == 'u' ? 4 : u[1] == 'U' ? 8 : 0;
		if (!n)
			return 0;
		for (size_t i = 0; i < n; i++) {
			int h = lex_hex_digit_value(u[2 + i]);

			if (h < 0)
				return 0;
			c = c << 4 | (uint32_t)h;
		}
		n += 2;
	} else if (u[0] >= 0x80) {
		n = lex_utf8_length(u);
		if (n == 1)
			return 0;
		c = n == 2 ? u[0] & 0x1f : n == 3 ? u[0] & 0x0f : u[0] & 0x07;
		for (size_t i = 1; i < n; i++)
			c = c << 6 | (u[i] & 0x3f);
	} else {
		return 0;
	}
	return lex_character_is_c11_identifier(c, first) ? n : 0;
}

static inline bool lex_character_is_exponent(char c)
{
	return c == 'e' || c == 'E' || c == 'p' || c == 'P';
}

/* end of a pp-number whose first character ends before p */
static inline size_t lex_number_run_end(const char *s, size_t p)
{
	while (lex_identifier_byte_table[(uint8_t)s[p]])
		p++;
	return p;
}

static size_t lex_number_end(const char *s, size_t p, unsigned *fl)
{
	for (;;) {
		char c;

		p = lex_number_run_end(s, p);
		c = s[p];
		if (c == '.' || ((c == '+' || c == '-') && lex_character_is_exponent(s[p - 1]))) {
			p++;
			continue;
		}
		if (unlikely((uint8_t)c >= 0x80 || (c == '\\' && lex_identifier_extended_length(s, p, false)))) {
			size_t l = lex_identifier_extended_length(s, p, false);

			if (l) {
				p += l;
				continue;
			}
		}
		if (unlikely(c == '\\')) {
			size_t q = lex_splices_skip(s, p);
			char d = s[q];

			size_t l;

			if (q != p && (lex_identifier_byte_table[(uint8_t)d] || d == '.' ||
				       ((d == '+' || d == '-') && lex_character_is_exponent(s[p - 1])))) {
				*fl |= LEX_TOKEN_FLAG_SPLICED;
				p = q + 1;
				continue;
			}
			if (q != p && (l = lex_identifier_extended_length(s, q, false))) {
				*fl |= LEX_TOKEN_FLAG_SPLICED;
				p = q + l;
				continue;
			}
		}
		return p;
	}
}

static inline size_t lex_identifier_end(const char *s, size_t p, unsigned *fl)
{
	p = lex_number_run_end(s, p);
	while (unlikely(s[p] == '\\' || (uint8_t)s[p] >= 0x80)) {
		size_t q = lex_splices_skip(s, p), l;

		if (q != p && lex_identifier_byte_table[(uint8_t)s[q]]) {
			*fl |= LEX_TOKEN_FLAG_SPLICED;
			p = lex_number_run_end(s, q);
		} else if ((l = lex_identifier_extended_length(s, q, false))) {
			if (q != p)
				*fl |= LEX_TOKEN_FLAG_SPLICED;
			p = lex_number_run_end(s, q + l);
		} else {
			break;
		}
	}
	return p;
}

/*
 * Character or string literal; p is at the opening quote.  Returns the end,
 * or 0 if it is unterminated (the caller makes an unknown token up to *end).
 */
static size_t lex_quoted_end(const char *s, size_t n, size_t p, uint8_t q, unsigned *fl, size_t *bad)
{
	for (p++;;) {
		uint8_t c;

		p = lex_quote_find(s, p, q);
		c = s[p];
		if (c == q)
			return p + 1;
		if (c == '\\') {
			size_t l = lex_splice_length(s, p);

			if (l) {
				*fl |= LEX_TOKEN_FLAG_SPLICED;
				p += l;
				continue;
			}
			/* escape: skip the next logical character */
			l = lex_splices_skip(s, p + 1);
			if (l != p + 1)
				*fl |= LEX_TOKEN_FLAG_SPLICED;
			if (s[l] == '\n' || s[l] == '\r' || l >= n) {
				*bad = l;
				return 0;
			}
			p = l + 1;
			continue;
		}
		if (c == '\n' || c == '\r' || p >= n) {
			*bad = p;
			return 0;
		}
		p++;	/* other control character */
	}
}

static inline bool lex_character_is_raw_delimiter(uint8_t c)
{
	return c > ' ' && c < 0x7f && c != '(' && c != ')' && c != '\\';
}

/* raw string literal; p is at the '"'.  0 if invalid (*bad = end) */
static size_t lex_raw_string_end(const char *s, size_t n, size_t p, size_t *bad)
{
	size_t d = p + 1, dl = 0, i;

	while (dl < 16 && lex_character_is_raw_delimiter(s[d + dl]))
		dl++;
	if (s[d + dl] != '(') {
		for (i = d; i < n && s[i] != '"'; i++)
			;
		*bad = i < n ? i + 1 : n;
		return 0;
	}
	for (i = d + dl + 1; i < n; i++)
		if (s[i] == ')' && i + 1 + dl < n + 1 && !memcmp(s + i + 1, s + d, dl) &&
		    s[i + 1 + dl] == '"')
			return i + 2 + dl;
	*bad = n;
	return 0;
}

/* length of the UTF-8 sequence at p (1 if invalid) */
static size_t lex_utf8_length(const uint8_t *s)
{
	if (s[0] >= 0xc2 && s[0] <= 0xdf && (s[1] & 0xc0) == 0x80)
		return 2;
	if (s[0] >= 0xe0 && s[0] <= 0xef && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80)
		return 3;
	if (s[0] >= 0xf0 && s[0] <= 0xf4 && (s[1] & 0xc0) == 0x80 && (s[2] & 0xc0) == 0x80 &&
	    (s[3] & 0xc0) == 0x80)
		return 4;
	return 1;
}

size_t lex_scratch_size(size_t size)
{
	/* four masks (with two spare words) and the identifier list */
	return LEX_MASK_COUNT * (size / 64 + 3) * sizeof(uint64_t) + (size + 2) * sizeof(uint32_t);
}

_Thread_local int lex_identifiers_not_interned;
static pthread_once_t lex_tables_once = PTHREAD_ONCE_INIT;

/*
 * Mask bits from position p: one unaligned load at byte p / 8, so only the
 * low 64 - p % 8 bits are valid, the rest are zero (m[(p >> 6) + 1] must
 * exist).
 */
static inline uint64_t lex_mask_window(const uint64_t *m, size_t p)
{
	uint64_t x;

	memcpy(&x, (const char *)m + (p >> 3), 8);
	return x >> (p & 7);
}

/*
 * Literal with an encoding prefix: the identifier-like prefix is st..p and
 * a quote (perhaps after splices) follows.  Returns the end of the literal
 * and sets *kind, or 0 if st..p is not a prefix (it is an identifier).
 */
static __attribute__((noinline)) size_t lex_literal_prefixed_end(const char *s, size_t n, size_t st, size_t p,
						 unsigned *fl, unsigned *kind)
{
	static const struct { char s[4]; uint8_t sq, dq; } pf[] = {
		{ "L", LEX_TOKEN_WIDE_CHARACTER, LEX_TOKEN_WIDE_STRING }, { "u", LEX_TOKEN_UTF16_CHARACTER, LEX_TOKEN_UTF16_STRING },
		{ "U", LEX_TOKEN_UTF32_CHARACTER, LEX_TOKEN_UTF32_STRING }, { "u8", 0, LEX_TOKEN_UTF8_STRING },
		{ "R", 0, LEX_TOKEN_STRING }, { "LR", 0, LEX_TOKEN_WIDE_STRING },
		{ "uR", 0, LEX_TOKEN_UTF16_STRING }, { "UR", 0, LEX_TOKEN_UTF32_STRING },
		{ "u8R", 0, LEX_TOKEN_UTF8_STRING },
	};
	size_t q = s[p] == '\\' ? lex_splices_skip(s, p) : p, l = 0, a, b, bad;
	uint8_t qc = (uint8_t)s[q];
	char pre[8];

	if (qc != '"' && qc != '\'')
		return 0;
	if (p - st <= 3 && !(*fl & LEX_TOKEN_FLAG_SPLICED)) {
		memcpy(pre, s + st, p - st);
		l = p - st;
	} else if (p - st < 64) {
		char tmp[64];
		lex_token tt = { .off = (uint32_t)st, .len = (uint32_t)(p - st), .flags = LEX_TOKEN_FLAG_SPLICED };

		l = lex_token_spell(s, &tt, tmp);
		if (l <= 3)
			memcpy(pre, tmp, l);
		else
			l = 0;
	}
	for (unsigned i = 0; l && i < sizeof(pf) / sizeof(pf[0]); i++) {
		if (strlen(pf[i].s) != l || memcmp(pf[i].s, pre, l))
			continue;
		if (qc == '"' && pre[l - 1] == 'R') {
			a = lex_raw_string_end(s, n, q, &bad);
			*kind = pf[i].dq;
			if (a)
				*fl |= LEX_TOKEN_FLAG_RAW_STRING;
		} else if (qc == '"') {
			a = lex_quoted_end(s, n, q, '"', fl, &bad);
			*kind = pf[i].dq;
		} else if (pf[i].sq) {
			b = lex_character_next(s, q);
			if (s[b] == '\'') {
				a = 0;
				bad = b + 1;
			} else {
				a = lex_quoted_end(s, n, q, '\'', fl, &bad);
			}
			*kind = pf[i].sq;
		} else {
			return 0;
		}
		if (q != p)
			*fl |= LEX_TOKEN_FLAG_SPLICED;
		if (!a) {
			*kind = LEX_TOKEN_UNKNOWN;
			a = bad;
		}
		return a;
	}
	return 0;
}

/* an identifier with a splice in it: intern its cleaned spelling */
static __attribute__((noinline)) uint32_t lex_identifier_dirty_intern(const char *s, size_t st, size_t p)
{
	char tmp[4096];
	lex_token tt = { .off = (uint32_t)st, .len = (uint32_t)(p - st), .flags = LEX_TOKEN_FLAG_SPLICED };
	size_t l = p - st < sizeof(tmp) ? lex_token_spell(s, &tt, tmp) : 0;

	return vintern_copy(tmp, l);
}

/*
 * intern the identifiers il[0..ni): their token addresses truncated to 32
 * bits, taken relative to out (a loop of its own: registers)
 */
static __attribute__((noinline)) void lex_identifiers_intern(vintern_cache *ic, const char *s, lex_token *out,
						  const uint32_t *il, uint32_t ni)
{
	for (uint32_t i = 0; i < ni; i++) {
		lex_token *k = (lex_token *)((char *)out + (uint32_t)(il[i] - (uint32_t)(uintptr_t)out));

		k->id = vintern_fast(ic, s + k->off, k->len);
	}
}

/*
 * Classify blocks [from, to) clamped to the file's nblk blocks; the block
 * holding the end gets nothing but X past it.  Returns the new end.
 */
static size_t lex_blocks_classify(const char *s, size_t n, size_t nblk, size_t from, size_t to,
			    uint64_t *m[LEX_MASK_COUNT])
{
	if (to > nblk)
		to = nblk;
	pp_paste_result_classify((const uint8_t *)s, from, to, m);
	if (from <= (n >> 6) && (n >> 6) < to) {
		for (int j = 0; j < LEX_MASK_COUNT; j++)
			if (j != LEX_MASK_HANDLER)
				m[j][n >> 6] &= ~(~0ull << (n & 63));
		m[LEX_MASK_HANDLER][n >> 6] |= ~0ull << (n & 63);
	}
	return to == nblk ? (size_t)-1 >> 1 : to;
}

/* a token with a splice before its first character starts at the splice */
#define FLAGS() ((uint8_t)(fl | (gap != st ? LEX_TOKEN_FLAG_SPLICED : 0)))

#define DISPATCH() do { STAT((uint8_t)s[p]); goto *disp[(uint8_t)s[st = p]]; } while (0)

#ifdef VLEX_STATS
unsigned long vlex_stat[512];
#define STAT(i) (vlex_stat[i]++)
#define STAT_T0() uint64_t t0_ = __builtin_readcyclecounter()
#define STAT_T1(i) (vlex_stat[i] += __builtin_readcyclecounter() - t0_)
__attribute__((destructor)) static void lex_statistics_dump(void)
{
	fprintf(stderr, "intern: lookups %lu local-miss %lu long %lu global-ticks %lu\n",
		vlex_stat[310], vlex_stat[311], vlex_stat[312], vlex_stat[321]);
}
#else
#define STAT(i) ((void)0)
#define STAT_T0() ((void)0)
#define STAT_T1(i) ((void)0)
#endif

/* the slow handlers return to the fast loop */
#define NEXT() goto fast

#define EMIT(K) do {							\
	t->off = (uint32_t)gap;						\
	t->len = (uint32_t)(p - gap);					\
	t->id = 0;							\
	t->kind = (K);							\
	t->flags = FLAGS();						\
	t++;								\
	fl = 0;								\
	gap = p;							\
	NEXT();								\
} while (0)

/* punctuator of k logical characters ending before e */
#define PUNCT(k, K, e) do {						\
	p = (e);							\
	if (unlikely(p - st != (k)))					\
		fl |= LEX_TOKEN_FLAG_SPLICED;						\
	EMIT(K);							\
} while (0)

/* identifier body: the first character ends before p */
#define IDENT_REST() do {						\
	p = lex_number_run_end(s, p);						\
	if (unlikely(s[p] == '\\' || (uint8_t)s[p] >= 0x80))		\
		p = lex_identifier_end(s, p, &fl);				\
} while (0)

/* clean identifiers are interned after the token loop, see il[] */
#define EMIT_IDENT() do {						\
	uint32_t i_ = 0;						\
	if (unlikely(lex_identifiers_not_interned))					\
		;							\
	else if (fl & LEX_TOKEN_FLAG_SPLICED)						\
		i_ = lex_identifier_dirty_intern(s, st, p);				\
	else if (gap == st)						\
		il[ni++] = (uint32_t)(uintptr_t)t;			\
	else								\
		i_ = vintern_fast(ic, s + st, p - st);			\
	t->off = (uint32_t)gap;						\
	t->len = (uint32_t)(p - gap);					\
	t->id = i_;							\
	t->kind = LEX_TOKEN_IDENTIFIER;						\
	t->flags = FLAGS();						\
	t++;								\
	fl = 0;								\
	gap = p;							\
	NEXT();								\
} while (0)

static uint32_t lex_tokenize_mode(const char *s, size_t n, lex_token *out, void *lex_scratch, bool digraphs)
{
	static const void *disp[256];
	size_t nb = n / 64 + 3, nblk = n / 64 + 1, p = 0, st = 0, q, a, b, bad;
	uint64_t *m[LEX_MASK_COUNT];
	uint32_t *il = (uint32_t *)((uint64_t *)lex_scratch + LEX_MASK_COUNT * nb), ni = 0;
	unsigned fl = LEX_TOKEN_FLAG_LINE_START, kind;
	vintern_cache *ic = vintern_local();
	lex_token *t = out;
	/*
	 * end of the last whitespace, comment or token: a token directly after
	 * a line splice starts at the splice, as in clang (it matters for
	 * __LINE__)
	 */
	size_t gap = 0;

	if (unlikely(!atomic_load_explicit((_Atomic(const void *) *)&disp[0], memory_order_acquire))) {
		static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

		pthread_mutex_lock(&lock);
		static const struct { const char *chars; const void *label; } map[] = {
			{ "[", &&l_lsquare }, { "]", &&l_rsquare }, { "(", &&l_lparen },
			{ ")", &&l_rparen }, { "{", &&l_lbrace }, { "}", &&l_rbrace },
			{ "~", &&l_tilde }, { "?", &&l_question }, { ";", &&l_semi },
			{ ",", &&l_comma }, { ".", &&l_period }, { "&", &&l_amp },
			{ "|", &&l_pipe }, { "+", &&l_plus }, { "-", &&l_minus },
			{ "*", &&l_star }, { "^", &&l_caret }, { "=", &&l_equal },
			{ "!", &&l_exclaim }, { "#", &&l_hash }, { ":", &&l_colon },
			{ "<", &&l_less }, { ">", &&l_greater }, { "%", &&l_percent },
			{ "/", &&l_slash }, { "\"", &&l_dquote }, { "'", &&l_squote },
			{ "\\", &&l_bslash }, { "LuUR", &&l_prefix },
			{ "0123456789", &&l_digit },
			{ " \t\v\f\n\r", &&l_space },
		};

		/*
		 * Re-check under the lock: a second initialization would
		 * briefly point '"', '/', '\\' ... at l_other while another
		 * thread is already lexing with the table.
		 */
		if (!atomic_load_explicit((_Atomic(const void *) *)&disp[0], memory_order_relaxed)) {
			pthread_once(&lex_tables_once, lex_tables_init);
			lex_structure_tables_init();
			for (int c = 1; c < 256; c++)
				disp[c] = c >= 0x80 ? &&l_high : lex_identifier_byte_table[c] ? &&l_ident : &&l_other;
			for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
				for (const char *c = map[i].chars; *c; c++)
					disp[(uint8_t)*c] = map[i].label;
			atomic_store_explicit((_Atomic(const void *) *)&disp[0], &&l_nul, memory_order_release);
		}
		pthread_mutex_unlock(&lock);
	}

	for (int i = 0; i < LEX_MASK_COUNT; i++)
		m[i] = (uint64_t *)lex_scratch + i * nb;
	/*
	 * Masks are computed lazily, CHUNK blocks ahead of the window, so the
	 * bytes of comments (a third of kernel headers) are never classified:
	 * the handlers skip them with NEON searches.
	 */
	size_t cl_end = 0;	/* blocks [.., cl_end) are classified */
#define CHUNK 8
#define ENSURE(w) do {							\
	if (unlikely((w) >= cl_end)) {					\
		size_t f_ = (w) - 1 > cl_end && (w) > 0 ? (w) - 1 : cl_end;	\
		cl_end = lex_blocks_classify(s, n, nblk, f_, f_ + CHUNK, m);	\
	}								\
} while (0)

	if (unlikely(lex_identifiers_not_interned == 2)) {
		lex_blocks_classify(s, n, nblk, 0, nb, m);
		return 0;
	}
	for (size_t i = nblk; i < nb; i++)
		for (int j = 0; j < LEX_MASK_COUNT; j++)
			m[j][i] = j == LEX_MASK_HANDLER ? ~0ull : 0;

	/*
	 * Structural pass over 64-byte windows: every token start in the
	 * window is computed at once from the masks, so consecutive tokens do
	 * not depend on each other and no branch depends on the token kind.
	 * At an X position (literal, comment, splice, ...) the handlers above
	 * take over for a token and come back here.
	 */
fast:
#ifdef VLEX_SCALAR
	DISPATCH();	/* reference build (test/fuzz/lexref.c): every token by its handler */
#endif
	if (unlikely(gap != p))
		DISPATCH();	/* a token directly after a splice */
	ENSURE((p >> 6) + 1);
	{
		/* N: [A-Za-z0-9_$.], P: punctuator characters and '.' */
		const uint64_t N = lex_mask_window(m[LEX_MASK_IDENTIFIER], p), W = lex_mask_window(m[LEX_MASK_WHITESPACE], p), NL = lex_mask_window(m[LEX_MASK_NEWLINE], p);
		const uint64_t D = lex_mask_window(m[LEX_MASK_DIGIT], p), P = lex_mask_window(m[LEX_MASK_PREFIX], p), DOT = N & P;
		const uint64_t PR = lex_mask_window(m[LEX_MASK_PAIR], p);
		/* the bits past the valid ones are an X: the window is 57..64 bytes */
		const unsigned valid = 64 - (unsigned)(p & 7);
		uint64_t X = lex_mask_window(m[LEX_MASK_HANDLER], p) | ~(~0ull >> (p & 7));
		/* pp-numbers: from a digit starting a run of [A-Za-z0-9_$.], or
		 * from a '.' before a digit, to the end of that run */
		const uint64_t A = (D & ~(N << 1)) | (DOT & (D >> 1));
		const uint64_t NUM = (N & ~(N + A)) | A;
		const uint64_t IDp = N & ~(NUM | P);
		const uint64_t NS = lex_mask_window(m[LEX_MASK_SIGN_NEXT], p);

		/*
		 * Left to the handlers (besides the X of pp_paste_result_classify(), which has
		 * every ".." with ellipses, "..5" and "..\<newline>."):
		 * overlapping pairs (<<=, ->=) and a number before + or -: in
		 * 1e+5 the sign belongs to it (numbers before a sign are rare).
		 */
		X |= (PR & (PR >> 1)) | (NS & NUM);

		const uint64_t IDs = IDp & ~(IDp << 1), NUMs = NUM & ~(NUM << 1);
		const uint64_t S = IDs | NUMs | (P & ~NUM & ~(PR << 1));
		const uint64_t B = S | W | X;
		/* whitespace runs that contain a newline, filled to their end */
		const uint64_t NLF = (W & ~(W + NL)) | NL;
		const uint64_t SP = W << 1, SOL = NLF << 1;
		const unsigned lim = X ? (unsigned)__builtin_ctzll(X) : 64;	/* <= valid */
		const uint8_t *kt = lex_punctuator_kind_table;

		__asm__("" : "+r"(kt));	/* in a register, not rematerialized per token */
		/*
		 * The token starts S and the token ends: boundaries after a
		 * non-blank byte.  Below lim they pair up.  Both bit-reversed,
		 * so that clz finds the next one (M1 has no ctz).
		 */
		uint64_t rs = __builtin_bitreverse64(S);
		uint64_t re = __builtin_bitreverse64(B & ~((W << 1) | 1));
		unsigned pend = fl;
		lex_token *const t0 = t;

		/*
		 * No test of rs: when the starts run out, the next end is at
		 * or past lim (64 if there is none).
		 */
		for (;;) {
			const unsigned e = (unsigned)__builtin_clzg(re, 64);

			if (e >= lim)
				break;	/* reaches X or past the window */

			const unsigned o = (unsigned)__builtin_clzll(rs);
			const size_t at = p + o;
			const unsigned f = ((SP >> o) & 1) * LEX_TOKEN_FLAG_SPACE_BEFORE | ((SOL >> o) & 1) * LEX_TOKEN_FLAG_LINE_START;
			uint16_t c01;

			memcpy(&c01, s + at, 2);
			/* kind without branches: ktab knows identifiers and numbers too */
			const unsigned kind = kt[c01];
			t->off = (uint32_t)at;
			t->len = e - o;
			t->id = 0;
			t->kind = (uint8_t)kind;
			t->flags = (uint8_t)(f | pend);
			pend = 0;
			/* see lex_identifiers_intern(); the asm keeps the compiler from a second induction variable */
			__asm__("" : "+r"(t));
			il[ni] = (uint32_t)(uintptr_t)t;
			ni += kind == LEX_TOKEN_IDENTIFIER;
			t++;
			rs ^= (1ull << 63) >> o;
			re ^= (1ull << 63) >> e;
		}
		fl = pend;
		if (likely(t != t0)) {
			/* the end of the last token */
			const unsigned last = (unsigned)(t[-1].off + t[-1].len - p);

			/*
			 * Only whitespace up to the X: take it and go to the
			 * handler, instead of a window that finds just that.
			 */
			if (lim < valid && !(~W & (~0ull << last) & ~(~0ull << lim))) {
				if (lim > last)
					fl |= LEX_TOKEN_FLAG_SPACE_BEFORE | (NL & (~0ull << last) & ~(~0ull << lim) ? LEX_TOKEN_FLAG_LINE_START : 0);
				p = gap = p + lim;
				DISPATCH();
			}
			p = gap = p + last;
			goto fast;
		}
		if (S && (unsigned)__builtin_ctzll(S) < lim) {
			/* the first token needs a handler (or is longer than the window) */
			const unsigned o = (unsigned)__builtin_ctzll(S);

			fl |= ((SP >> o) & 1) * LEX_TOKEN_FLAG_SPACE_BEFORE | ((SOL >> o) & 1) * LEX_TOKEN_FLAG_LINE_START;
			p = gap = p + o;
			DISPATCH();
		}
		/* whitespace, then an X or the next window */
		{
			const unsigned o = ~W ? (unsigned)__builtin_ctzll(~W) : 64;	/* <= valid */

			if (o) {
				fl |= LEX_TOKEN_FLAG_SPACE_BEFORE | (NL & (o == 64 ? ~0ull : (1ull << o) - 1) ? LEX_TOKEN_FLAG_LINE_START : 0);
				p = gap = p + o;
			}
			if (o >= valid)
				goto fast;
			DISPATCH();
		}
	}

l_ident:
	p++;
	IDENT_REST();
	EMIT_IDENT();
l_prefix:
	p++;
	IDENT_REST();
	if (unlikely(s[p] == '"' || s[p] == '\'' || s[p] == '\\') &&
	    (q = lex_literal_prefixed_end(s, n, st, p, &fl, &kind))) {
		p = q;
		EMIT(kind);
	}
	EMIT_IDENT();
l_digit:
	p = lex_number_end(s, p + 1, &fl);
	EMIT(LEX_TOKEN_NUMBER);
l_space:	/* only after a NUL or a splice the masks did not cover */
	fl |= LEX_TOKEN_FLAG_SPACE_BEFORE | (s[p] == '\n' || s[p] == '\r' ? LEX_TOKEN_FLAG_LINE_START : 0);
	gap = ++p;
	NEXT();
l_nul:
	if (p >= n)
		goto done;
	gap = ++p;
	fl |= LEX_TOKEN_FLAG_SPACE_BEFORE;
	NEXT();
l_bslash:
	if ((q = lex_splice_length(s, p))) {
		p += q;
		NEXT();
	}
	if ((q = lex_identifier_extended_length(s, p, true))) {
		p = lex_identifier_end(s, p + q, &fl);
		EMIT_IDENT();
	}
	/* a well-formed UCN that may not start an identifier */
	q = s[p + 1] == 'u' ? 4 : s[p + 1] == 'U' ? 8 : 0;
	for (a = 0; a < q && lex_hex_digit_value((uint8_t)s[p + 2 + a]) >= 0; a++)
		;
	p += q && a == q ? q + 2 : 1;
	EMIT(LEX_TOKEN_UNKNOWN);
l_dquote:
	q = lex_quoted_end(s, n, p, '"', &fl, &bad);
	kind = LEX_TOKEN_STRING;
	goto lit;
l_squote:
	if (s[a = lex_character_next(s, p)] == '\'') {
		q = 0;
		bad = a + 1;
		if (a != p + 1)
			fl |= LEX_TOKEN_FLAG_SPLICED;
	} else {
		q = lex_quoted_end(s, n, p, '\'', &fl, &bad);
	}
	kind = LEX_TOKEN_CHARACTER;
lit:
	if (q) {
		p = q;
		EMIT(kind);
	}
	p = bad;
	EMIT(LEX_TOKEN_UNKNOWN);
l_high:
	if ((q = lex_identifier_extended_length(s, p, true))) {
		p = lex_identifier_end(s, p + q, &fl);
		EMIT_IDENT();
	}
	p += lex_utf8_length((const uint8_t *)s + p);
	EMIT(LEX_TOKEN_UNKNOWN);
l_other:
	p++;
	EMIT(LEX_TOKEN_UNKNOWN);

l_lsquare:	PUNCT(1, LEX_TOKEN_LEFT_SQUARE, p + 1);
l_rsquare:	PUNCT(1, LEX_TOKEN_RIGHT_SQUARE, p + 1);
l_lparen:	PUNCT(1, LEX_TOKEN_LEFT_PAREN, p + 1);
l_rparen:	PUNCT(1, LEX_TOKEN_RIGHT_PAREN, p + 1);
l_lbrace:	PUNCT(1, LEX_TOKEN_LEFT_BRACE, p + 1);
l_rbrace:	PUNCT(1, LEX_TOKEN_RIGHT_BRACE, p + 1);
l_tilde:	PUNCT(1, LEX_TOKEN_TILDE, p + 1);
l_question:	PUNCT(1, LEX_TOKEN_QUESTION, p + 1);
l_semi:		PUNCT(1, LEX_TOKEN_SEMICOLON, p + 1);
l_comma:	PUNCT(1, LEX_TOKEN_COMMA, p + 1);
l_period:
	a = lex_character_next(s, p);
	if (s[a] >= '0' && s[a] <= '9') {
		if (a != p + 1)
			fl |= LEX_TOKEN_FLAG_SPLICED;
		p = lex_number_end(s, a + 1, &fl);
		EMIT(LEX_TOKEN_NUMBER);
	}
	if (s[a] == '.' && s[b = lex_character_next(s, a)] == '.')
		PUNCT(3, LEX_TOKEN_ELLIPSIS, b + 1);
	PUNCT(1, LEX_TOKEN_PERIOD, p + 1);
l_amp:
	a = lex_character_next(s, p);
	if (s[a] == '&') PUNCT(2, LEX_TOKEN_AMPERSAND_AMPERSAND, a + 1);
	if (s[a] == '=') PUNCT(2, LEX_TOKEN_AMPERSAND_EQUAL, a + 1);
	PUNCT(1, LEX_TOKEN_AMPERSAND, p + 1);
l_pipe:
	a = lex_character_next(s, p);
	if (s[a] == '|') PUNCT(2, LEX_TOKEN_PIPE_PIPE, a + 1);
	if (s[a] == '=') PUNCT(2, LEX_TOKEN_PIPE_EQUAL, a + 1);
	PUNCT(1, LEX_TOKEN_PIPE, p + 1);
l_plus:
	a = lex_character_next(s, p);
	if (s[a] == '+') PUNCT(2, LEX_TOKEN_PLUS_PLUS, a + 1);
	if (s[a] == '=') PUNCT(2, LEX_TOKEN_PLUS_EQUAL, a + 1);
	PUNCT(1, LEX_TOKEN_PLUS, p + 1);
l_minus:
	a = lex_character_next(s, p);
	if (s[a] == '>') PUNCT(2, LEX_TOKEN_ARROW, a + 1);
	if (s[a] == '-') PUNCT(2, LEX_TOKEN_MINUS_MINUS, a + 1);
	if (s[a] == '=') PUNCT(2, LEX_TOKEN_MINUS_EQUAL, a + 1);
	PUNCT(1, LEX_TOKEN_MINUS, p + 1);
l_star:
	a = lex_character_next(s, p);
	if (s[a] == '=') PUNCT(2, LEX_TOKEN_STAR_EQUAL, a + 1);
	PUNCT(1, LEX_TOKEN_STAR, p + 1);
l_caret:
	a = lex_character_next(s, p);
	if (s[a] == '=') PUNCT(2, LEX_TOKEN_CARET_EQUAL, a + 1);
	PUNCT(1, LEX_TOKEN_CARET, p + 1);
l_equal:
	a = lex_character_next(s, p);
	if (s[a] == '=') PUNCT(2, LEX_TOKEN_EQUAL_EQUAL, a + 1);
	PUNCT(1, LEX_TOKEN_EQUAL, p + 1);
l_exclaim:
	a = lex_character_next(s, p);
	if (s[a] == '=') PUNCT(2, LEX_TOKEN_EXCLAMATION_EQUAL, a + 1);
	PUNCT(1, LEX_TOKEN_EXCLAMATION, p + 1);
l_hash:
	a = lex_character_next(s, p);
	if (s[a] == '#') PUNCT(2, LEX_TOKEN_HASH_HASH, a + 1);
	PUNCT(1, LEX_TOKEN_HASH, p + 1);
l_colon:
	a = lex_character_next(s, p);
	if (s[a] == ':') PUNCT(2, LEX_TOKEN_COLON_COLON, a + 1);
	if (s[a] == '>' && digraphs) PUNCT(2, LEX_TOKEN_RIGHT_SQUARE, a + 1);
	PUNCT(1, LEX_TOKEN_COLON, p + 1);
l_less:
	a = lex_character_next(s, p);
	if (s[a] == '<') {
		if (s[b = lex_character_next(s, a)] == '=') PUNCT(3, LEX_TOKEN_LESS_LESS_EQUAL, b + 1);
		PUNCT(2, LEX_TOKEN_LESS_LESS, a + 1);
	}
	if (s[a] == '=') PUNCT(2, LEX_TOKEN_LESS_EQUAL, a + 1);
	if (s[a] == ':' && digraphs) PUNCT(2, LEX_TOKEN_LEFT_SQUARE, a + 1);
	if (s[a] == '%' && digraphs) PUNCT(2, LEX_TOKEN_LEFT_BRACE, a + 1);
	PUNCT(1, LEX_TOKEN_LESS, p + 1);
l_greater:
	a = lex_character_next(s, p);
	if (s[a] == '>') {
		if (s[b = lex_character_next(s, a)] == '=') PUNCT(3, LEX_TOKEN_GREATER_GREATER_EQUAL, b + 1);
		PUNCT(2, LEX_TOKEN_GREATER_GREATER, a + 1);
	}
	if (s[a] == '=') PUNCT(2, LEX_TOKEN_GREATER_EQUAL, a + 1);
	PUNCT(1, LEX_TOKEN_GREATER, p + 1);
l_percent:
	a = lex_character_next(s, p);
	if (s[a] == '=') PUNCT(2, LEX_TOKEN_PERCENT_EQUAL, a + 1);
	if (s[a] == '>' && digraphs) PUNCT(2, LEX_TOKEN_RIGHT_BRACE, a + 1);
	if (s[a] == ':' && digraphs) {
		b = lex_character_next(s, a);
		if (s[b] == '%' && s[q = lex_character_next(s, b)] == ':') PUNCT(4, LEX_TOKEN_HASH_HASH, q + 1);
		PUNCT(2, LEX_TOKEN_HASH, a + 1);
	}
	PUNCT(1, LEX_TOKEN_PERCENT, p + 1);
l_slash:
	a = lex_character_next(s, p);
	if (s[a] == '*') {
		/* block comment: stops at its end, at backslashes (a splice in the end?) and at NULs */
		for (q = a + 1;; q++) {
			q = lex_comment_end_find(s, q);
			if (q >= n) {
				/* unterminated: an error token */
				p = n;
				EMIT(LEX_TOKEN_UNKNOWN);
			}
			if (s[q] == '*') {
				q += 2;
				break;
			}
			if (s[q] == '\\' && q - 1 > a && s[q - 1] == '*' &&
			    s[b = lex_splices_skip(s, q)] == '/' && b != q) {
				q = b + 1;
				break;
			}
		}
		p = gap = q;
		fl |= LEX_TOKEN_FLAG_SPACE_BEFORE;
		NEXT();
	}
	if (s[a] == '/') {
		/* line comment: up to an unspliced \n or \r */
		for (q = a + 1;;) {
			b = lex_line_end_find(s, q);
			if (b >= n)
				break;
			if (!s[b]) {
				q = b + 1;	/* NUL in the comment */
				continue;
			}
			size_t e = b;

			if (e > q && (s[e - 1] == '\r' || s[e - 1] == '\n') && s[e - 1] != s[b])
				e--;	/* second half of \r\n or \n\r */
			while (e > q && (s[e - 1] == ' ' || s[e - 1] == '\t' ||
					 s[e - 1] == '\v' || s[e - 1] == '\f'))
				e--;
			if (e == q || s[e - 1] != '\\' || !lex_splice_length(s, e - 1))
				break;
			q = b + 1;
			/* skip the other half of this splice's newline */
			if ((s[q] == '\n' || s[q] == '\r') && s[q] != s[b])
				q++;
		}
		p = gap = b < n ? b : n;
		fl |= LEX_TOKEN_FLAG_SPACE_BEFORE;
		NEXT();
	}
	if (s[a] == '=') PUNCT(2, LEX_TOKEN_SLASH_EQUAL, a + 1);
	PUNCT(1, LEX_TOKEN_SLASH, p + 1);

done:
	t->off = (uint32_t)n;
	t->len = 0;
	t->id = 0;
	t->kind = LEX_TOKEN_EOF;
	t->flags = (uint8_t)fl;
	/* intern the clean identifiers: no unpredictable branches left here */
	STAT_T0();
	if (!lex_identifiers_not_interned)
		lex_identifiers_intern(ic, s, out, il, ni);
	STAT_T1(320);
	return (uint32_t)(t - out);
}

uint32_t lex_tokenize(const char *s, size_t n, lex_token *out, void *lex_scratch)
{
	return lex_tokenize_mode(s, n, out, lex_scratch, true);
}

size_t lex_token_spell(const char *s, const lex_token *t, char *buf)
{
	size_t p = t->off, e = t->off + t->len, n = 0;

	if (!(t->flags & LEX_TOKEN_FLAG_SPLICED)) {
		memcpy(buf, s + p, t->len);
		return t->len;
	}
	while (p < e) {
		size_t l = lex_splice_length(s, p);

		if (l) {
			p += l;
			continue;
		}
		if ((t->flags & LEX_TOKEN_FLAG_RAW_STRING) && s[p] == '"') {
			/* raw string body: splices are not removed */
			memcpy(buf + n, s + p, e - p);
			return n + (e - p);
		}
		buf[n++] = s[p++];
	}
	return n;
}

/* ---- token cache ---------------------------------------------------- */

/* per-thread bump arena: virtual reservations, pages faulted on use */
static _Thread_local char *lex_region_cursor, *lex_region_end;

static void *lex_region_allocate(size_t n)
{
	char *p;

	n = (n + 63) & ~(size_t)63;
	if ((size_t)(lex_region_end - lex_region_cursor) < n) {
		size_t sz = n > (64u << 20) ? n : (64u << 20);

		p = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (p == MAP_FAILED) {
			perror("vcc: mmap");
			abort();
		}
		lex_region_cursor = p;
		lex_region_end = p + sz;
	}
	p = lex_region_cursor;
	lex_region_cursor += n;
	return p;
}

/* give back the unused tail of the last lex_region_allocate() allocation */
static void lex_region_trim(void *p, size_t used)
{
	lex_region_cursor = (char *)p + ((used + 63) & ~(size_t)63);
}

static _Thread_local void *lex_scratch;
static _Thread_local size_t lex_scratch_bytes;

/*
 * The file's bytes, read into the arena with 128 zero bytes after them (the
 * lexer reads ahead).  read() beats mmap here: no mapping per file, no
 * second mapping for files whose last page has no slack, fewer faults.
 * Returns the bytes actually read (the file may have shrunk meanwhile).
 */
static char *lex_file_read(int fd, size_t size, size_t *got)
{
	char *b = lex_region_allocate(size + 128);	/* not always fresh: lex_region_trim gives tails back */
	size_t n = 0;

	while (n < size) {
		ssize_t r = read(fd, b + n, size - n);

		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			break;
		n += (size_t)r;
	}
	memset(b + n, 0, 128);
	/* a UTF-8 byte order mark is not text (clang skips it): blanks keep the columns */
	if (n >= 3 && (unsigned char)b[0] == 0xef && (unsigned char)b[1] == 0xbb && (unsigned char)b[2] == 0xbf)
		b[0] = b[1] = b[2] = ' ';
	*got = n;
	return b;
}

/* open + read: the file's bytes in this thread's arena, false if unreadable */
static bool lex_source_read(const char *path, const char **src, size_t *size, uint64_t id[4])
{
	struct stat st;
	int fd;

	do
		fd = open(path, O_RDONLY | O_CLOEXEC);
	while (fd < 0 && errno == EINTR);
	if (fd < 0)
		return false;
	if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size >= (1ll << 31)) {
		close(fd);
		return false;
	}
	*src = lex_file_read(fd, (size_t)st.st_size, size);
	id[0] = (uint64_t)st.st_dev;
	id[1] = (uint64_t)st.st_ino;
	id[2] = (uint64_t)st.st_size;
#ifdef __APPLE__
	id[3] = (uint64_t)st.st_mtimespec.tv_sec * 1000000000u + (uint64_t)st.st_mtimespec.tv_nsec;
#else
	id[3] = (uint64_t)st.st_mtim.tv_sec * 1000000000u + (uint64_t)st.st_mtim.tv_nsec;
#endif
	close(fd);
	return true;
}

/* lex src (from lex_source_read) into f, tokens in this thread's arena */
static void lex_source_tokenize_mode(lex_file *f, const char *src, size_t size, bool digraphs)
{
	size_t need = lex_scratch_size(size);
	lex_token *tok;

	if (need > lex_scratch_bytes) {
		if (lex_scratch)
			munmap(lex_scratch, lex_scratch_bytes);
		lex_scratch_bytes = need < (1u << 20) ? (1u << 20) : need * 2;
		lex_scratch = mmap(NULL, lex_scratch_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (lex_scratch == MAP_FAILED)
			abort();
	}
	tok = lex_region_allocate((size + 2) * sizeof(lex_token));
	f->src = src;
	f->size = (uint32_t)size;
	f->ntok = lex_tokenize_mode(src, size, tok, lex_scratch, digraphs);
	f->tok = tok;
	lex_region_trim(tok, (f->ntok + 1) * sizeof(lex_token));
}

static void lex_source_tokenize(lex_file *f, const char *src, size_t size)
{
	lex_source_tokenize_mode(f, src, size, true);
}

/* Strict C90 uses private tokens; never change the shared cached file.
 * Retain source bytes and offsets, including digraph-looking text in strings
 * and comments and the spacing of adjacent tokens used by stringization. */
const lex_file *lex_file_without_digraphs(const lex_file *f)
{
	lex_file *nf = lex_region_allocate(sizeof(*nf));

	memset(nf, 0, sizeof(*nf));
	nf->path = f->path;
	nf->dev = f->dev;
	nf->ino = f->ino;
	lex_source_tokenize_mode(nf, f->src, f->size, false);
	return nf;
}

static bool lex_file_load(lex_file *f, const char *path, uint64_t sm[2])
{
	const char *src;
	size_t size;

	uint64_t id[4];

	if (!lex_source_read(path, &src, &size, id))
		return false;
	f->dev = id[0];
	f->ino = id[1];
	sm[0] = id[2];
	sm[1] = id[3];
	lex_source_tokenize(f, src, size);
	return true;
}

/* benchmarking: lex path as lex_file_get() would, without the cache (leaks) */
int lex_file_load_uncached(const char *path, lex_file *f)
{
	uint64_t sm[2];

	return lex_file_load(f, path, sm);
}

/*
 * Cache entry states.  QUEUED: waiting for an I/O thread (lex_file_prefetch);
 * READING: an I/O thread has it; READ: bytes in f.src, not lexed; LOADING:
 * a lexing thread has it; OK / MISSING: final.
 */
enum { LEX_CACHE_QUEUED, LEX_CACHE_READING, LEX_CACHE_READ, LEX_CACHE_LOADING, LEX_CACHE_OK, LEX_CACHE_MISSING };

typedef struct lex_cache_entry {
	lex_file f;
	struct lex_cache_entry *next;	/* the bucket's chain, newest first; set before publication */
	uint32_t hash, len;
	_Atomic int state;
	_Atomic uint32_t epoch;	/* the cache epoch this entry was last known to match the file system in */
	_Atomic int dead;	/* found changed: later lookups skip it (the file is read again) */
	uint64_t sm[2];		/* size and mtime (ns) of the file as read */
	char path[];
} lex_cache_entry;

/*
 * Validation (vccd, Q-SCHED-01).  A new make that starts while jobs of another
 * run cannot flush the caches; instead vcache_new_epoch() starts a new epoch,
 * and a final entry of an older one is checked once (a stat: file identity,
 * size, mtime; for a missing file, that it is still missing) before it is
 * used again.  A changed entry is marked dead and read afresh (old entries
 * stay allocated: pointers into them stay valid).  The directory and include
 * caches check the same epoch.
 */
static _Atomic uint32_t cache_epoch = 1;

uint32_t vcache_epoch(void)
{
	return atomic_load_explicit(&cache_epoch, memory_order_acquire);
}

void vcache_new_epoch(void)
{
	atomic_fetch_add_explicit(&cache_epoch, 1, memory_order_acq_rel);
}

static bool entry_fresh(const lex_cache_entry *e, int st)
{
	struct stat s;

	if (stat(e->path, &s))
		return st == LEX_CACHE_MISSING;
	if (st != LEX_CACHE_OK)
		return false;	/* it exists now */
	return S_ISREG(s.st_mode) && (uint64_t)s.st_dev == e->f.dev && (uint64_t)s.st_ino == e->f.ino &&
	       (uint64_t)s.st_size == e->sm[0] &&
#ifdef __APPLE__
	       (uint64_t)s.st_mtimespec.tv_sec * 1000000000u + (uint64_t)s.st_mtimespec.tv_nsec == e->sm[1];
#else
	       (uint64_t)s.st_mtim.tv_sec * 1000000000u + (uint64_t)s.st_mtim.tv_nsec == e->sm[1];
#endif
}

/*
 * Chained, never full: every probed path (missing ones too) gets an entry,
 * and a long batch (vccd, vpp --batch) probes far more than any table size.
 */
#define LEX_CACHE_TABLE_BITS 16
static _Atomic(lex_cache_entry *) lex_cache_table[1u << LEX_CACHE_TABLE_BITS];

/* the entry for path, inserted in state st0 if absent (*ins set then) */
static lex_cache_entry *lex_cache_entry_get(const char *path, size_t len, int st0, bool *ins)
{
	uint32_t h = vintern_hash(path, len);
	_Atomic(lex_cache_entry *) *b = &lex_cache_table[h >> (32 - LEX_CACHE_TABLE_BITS)];
	lex_cache_entry *head = atomic_load_explicit(b, memory_order_acquire), *seen = NULL, *mine = NULL;

	*ins = false;
	for (;;) {
		for (lex_cache_entry *e = head; e != seen; e = e->next)
			if (e->hash == h && e->len == len && !memcmp(e->path, path, len) &&
			    !atomic_load_explicit(&e->dead, memory_order_relaxed))
				return e;	/* mine, if allocated, is wasted arena space: rare */
		if (!mine) {
			mine = lex_region_allocate(sizeof(*mine) + len + 1);
			memset(mine, 0, sizeof(*mine));
			mine->hash = h;
			mine->len = (uint32_t)len;
			memcpy(mine->path, path, len);
			mine->path[len] = 0;
			mine->f.path = mine->path;
			atomic_init(&mine->state, st0);
			atomic_init(&mine->epoch, vcache_epoch());
		}
		mine->next = head;
		seen = head;	/* scanned: only entries pushed after it are new */
		if (atomic_compare_exchange_strong_explicit(b, &head, mine, memory_order_acq_rel,
							    memory_order_acquire)) {
			*ins = true;
			return mine;
		}
	}
}

static bool lex_cache_entry_claim(lex_cache_entry *e, int from, int to)
{
	return atomic_compare_exchange_strong_explicit(&e->state, &from, to,
			memory_order_acq_rel, memory_order_acquire);
}

static void pp_header_cache_entry_publish(lex_cache_entry *e, bool ok)
{
	atomic_store_explicit(&e->state, ok ? LEX_CACHE_OK : LEX_CACHE_MISSING, memory_order_release);
}

const lex_file *lex_file_trigraphs_replace(const lex_file *f)
{
	static const char from[] = "=/'()!<>-", to[] = "#\\^[]|{}~";
	const char *s = f->src, *q = memmem(s, f->size, "??", 2);
	size_t n = f->size, o = 0;
	char *b;
	lex_file *nf;

	while (q && (q + 2 >= s + n || !strchr(from, q[2]) || !q[2]))
		q = q + 1 < s + n ? memmem(q + 1, (size_t)(s + n - q - 1), "??", 2) : NULL;
	if (!q)
		return f;
	b = lex_region_allocate(n + 128);
	for (size_t i = 0; i < n; i++) {
		const char *t;

		if (s[i] == '?' && i + 2 < n && s[i + 1] == '?' && s[i + 2] && (t = strchr(from, s[i + 2]))) {
			b[o++] = to[t - from];
			i += 2;
		} else {
			b[o++] = s[i];
		}
	}
	memset(b + o, 0, 128);
	nf = lex_region_allocate(sizeof(*nf));
	memset(nf, 0, sizeof(*nf));
	nf->path = f->path;
	nf->dev = f->dev;
	nf->ino = f->ino;
	lex_source_tokenize(nf, b, o);
	return nf;
}

static bool lex_character_is_alphanumeric(char ch)
{
	return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

/* end of the comment or literal at s[i] (s[i] is '/' or a quote), i + 1 if neither */
static size_t lex_literal_or_comment_skip(const char *s, size_t n, size_t i)
{
	if (s[i] == '/' && i + 1 < n && s[i + 1] == '/') {
		while (i < n && s[i] != '\n')
			i += s[i] == '\\' && i + 1 < n ? 2 : 1;
		return i;
	}
	if (s[i] == '/' && i + 1 < n && s[i + 1] == '*') {
		const char *e = memmem(s + i + 2, n - i - 2, "*/", 2);

		return e ? (size_t)(e - s) + 2 : n;
	}
	if (s[i] == '"' || s[i] == '\'') {
		char q = s[i++];

		while (i < n && s[i] != q && s[i] != '\n')
			i += s[i] == '\\' && i + 1 < n ? 2 : 1;
		return i < n ? i + 1 : n;
	}
	return i + 1;
}

const lex_file *lex_file_digit_separators_remove(const lex_file *f)
{
	const char *s = f->src;
	size_t n = f->size, o = 0, i = 0;
	bool any = false;
	char *b;
	lex_file *nf;

	/* quick: a ' between two alphanumerics somewhere at all */
	for (const char *q = memchr(s, '\'', n); q; q = memchr(q + 1, '\'', (size_t)(s + n - q - 1)))
		if (q > s && q + 1 < s + n && lex_character_is_alphanumeric(q[-1]) && lex_character_is_alphanumeric(q[1])) {
			any = true;
			break;
		}
	if (!any)
		return f;
	b = lex_region_allocate(n + 128);
	any = false;
	while (i < n) {
		char ch = s[i];
		size_t j;

		if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_') {
			for (j = i; j < n && lex_character_is_alphanumeric(s[j]); j++)
				;
		} else if ((ch >= '0' && ch <= '9') || (ch == '.' && i + 1 < n && s[i + 1] >= '0' && s[i + 1] <= '9')) {
			/* a pp-number: digit separators (' between alphanumerics) dropped */
			for (j = i; j < n;) {
				if (lex_character_is_alphanumeric(s[j]) || s[j] == '.') {
					if ((s[j] == 'e' || s[j] == 'E' || s[j] == 'p' || s[j] == 'P') && j + 1 < n &&
					    (s[j + 1] == '+' || s[j + 1] == '-'))
						b[o++] = s[j++];
					b[o++] = s[j++];
				} else if (s[j] == '\'' && j + 1 < n && lex_character_is_alphanumeric(s[j + 1])) {
					j++;
					any = true;
				} else {
					break;
				}
			}
			i = j;
			continue;
		} else if (ch == '/' || ch == '"' || ch == '\'') {
			j = lex_literal_or_comment_skip(s, n, i);
		} else {
			j = i + 1;
		}
		memcpy(b + o, s + i, j - i);
		o += j - i;
		i = j;
	}
	if (!any)
		return f;
	memset(b + o, 0, 128);
	nf = lex_region_allocate(sizeof(*nf));
	memset(nf, 0, sizeof(*nf));
	nf->path = f->path;
	nf->dev = f->dev;
	nf->ino = f->ino;
	lex_source_tokenize(nf, b, o);
	return nf;
}

/* C89 (strict): no line comments; "//" outside comments and literals is
 * two '/' tokens.  The copy has a space between them, so a column after one
 * moves by one. */
const lex_file *lex_file_c89_convert(const lex_file *f)
{
	const char *s = f->src;
	size_t n = f->size, o = 0, i = 0, cnt = 0;
	char *b;
	lex_file *nf;

	if (!memmem(s, n, "//", 2))
		return f;
	for (const char *q = s; (q = memmem(q, (size_t)(s + n - q), "//", 2)); q++)
		cnt++;
	b = lex_region_allocate(n + cnt + 128);
	cnt = 0;
	while (i < n) {
		char ch = s[i];
		size_t j;

		if (ch == '/' && i + 1 < n && s[i + 1] == '/') {
			b[o++] = '/';
			b[o++] = ' ';
			i++;
			cnt++;
			continue;
		}
		j = ch == '/' || ch == '"' || ch == '\'' ? lex_literal_or_comment_skip(s, n, i) : i + 1;
		memcpy(b + o, s + i, j - i);
		o += j - i;
		i = j;
	}
	if (!cnt)
		return f;
	memset(b + o, 0, 128);
	nf = lex_region_allocate(sizeof(*nf));
	memset(nf, 0, sizeof(*nf));
	nf->path = f->path;
	nf->dev = f->dev;
	nf->ino = f->ino;
	lex_source_tokenize(nf, b, o);
	return nf;
}

const lex_file *(*lex_file_get_hook)(const char *path, size_t len);

/* forget every file (a new build: vccd, idle): later lookups read and lex
 * again.  Old entries are not freed, so pointers into them stay valid. */
void lex_cache_flush(void)
{
	for (size_t i = 0; i < (1u << LEX_CACHE_TABLE_BITS); i++)
		atomic_store_explicit(&lex_cache_table[i], NULL, memory_order_release);
}

const lex_file *lex_file_get(const char *path, size_t len)
{
	bool ins;
	lex_cache_entry *e;

	if (unlikely(lex_file_get_hook != NULL))
		return lex_file_get_hook(path, len);
	e = lex_cache_entry_get(path, len, LEX_CACHE_LOADING, &ins);

	if (ins) {
		bool ok = lex_file_load(&e->f, e->path, e->sm);

		pp_header_cache_entry_publish(e, ok);
		return ok ? &e->f : NULL;
	}
	for (int spin = 0;; spin++) {
		int st = atomic_load_explicit(&e->state, memory_order_acquire);

		if (st == LEX_CACHE_OK || st == LEX_CACHE_MISSING) {
			uint32_t cur = vcache_epoch();

			if (likely(atomic_load_explicit(&e->epoch, memory_order_relaxed) == cur))
				return st == LEX_CACHE_OK ? &e->f : NULL;
			if (entry_fresh(e, st)) {	/* (no lock: threads that race each stat once) */
				atomic_store_explicit(&e->epoch, cur, memory_order_relaxed);
				return st == LEX_CACHE_OK ? &e->f : NULL;
			}
			atomic_store_explicit(&e->dead, 1, memory_order_relaxed);
			return lex_file_get(path, len);	/* a fresh entry, read now */
		}
		if (st == LEX_CACHE_QUEUED && lex_cache_entry_claim(e, LEX_CACHE_QUEUED, LEX_CACHE_LOADING)) {
			/* not reached by the I/O threads yet: do it here */
			bool ok = lex_file_load(&e->f, e->path, e->sm);

			pp_header_cache_entry_publish(e, ok);
			return ok ? &e->f : NULL;
		}
		if (st == LEX_CACHE_READ && lex_cache_entry_claim(e, LEX_CACHE_READ, LEX_CACHE_LOADING)) {
			lex_source_tokenize(&e->f, e->f.src, e->f.size);
			pp_header_cache_entry_publish(e, true);
			return &e->f;
		}
#if defined(__aarch64__)
		if (spin < 64)
			__asm__ volatile("yield");
		else
#elif defined(__x86_64__)
		if (spin < 64)
			__asm__ volatile("pause");
		else
#endif
			sched_yield();
	}
}

/*
 * Read-ahead.  open() is effectively serialized in the macOS kernel
 * (~6 us per file however many threads call it, and each extra thread only
 * adds contention), so a few I/O threads open and read files that will be
 * needed, and the lexing threads only lex.
 */
enum { LEX_PREFETCH_THREADS = 3 };
static pthread_mutex_t lex_prefetch_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t lex_prefetch_queue_condition = PTHREAD_COND_INITIALIZER;
static lex_cache_entry **lex_prefetch_queue;
static size_t lex_prefetch_queue_head, lex_prefetch_queue_tail, lex_prefetch_queue_capacity;

static void *lex_prefetch_thread_run(void *arg)
{
	(void)arg;
#ifdef __APPLE__
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
	for (;;) {
		lex_cache_entry *e;

		pthread_mutex_lock(&lex_prefetch_queue_mutex);
		while (lex_prefetch_queue_head == lex_prefetch_queue_tail)
			pthread_cond_wait(&lex_prefetch_queue_condition, &lex_prefetch_queue_mutex);
		e = lex_prefetch_queue[lex_prefetch_queue_head++ % lex_prefetch_queue_capacity];
		pthread_mutex_unlock(&lex_prefetch_queue_mutex);
		if (lex_cache_entry_claim(e, LEX_CACHE_QUEUED, LEX_CACHE_READING)) {
			const char *src;
			size_t size;

			uint64_t id[4];

			if (lex_source_read(e->path, &src, &size, id)) {
				e->f.dev = id[0];
				e->f.ino = id[1];
				e->sm[0] = id[2];
				e->sm[1] = id[3];
				e->f.src = src;
				e->f.size = (uint32_t)size;
				atomic_store_explicit(&e->state, LEX_CACHE_READ, memory_order_release);
			} else {
				pp_header_cache_entry_publish(e, false);
			}
		}
	}
	return NULL;
}

static void lex_prefetch_threads_start(void)
{
	const char *e = getenv("VLEX_IO_THREADS");
	int n = e ? atoi(e) : LEX_PREFETCH_THREADS;

	for (int i = 0; i < (n > 0 && n <= 32 ? n : LEX_PREFETCH_THREADS); i++) {
		pthread_t t;

		pthread_create(&t, NULL, lex_prefetch_thread_run, NULL);
		pthread_detach(t);
	}
}

void lex_file_prefetch(const char *path, size_t len)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	bool ins;
	lex_cache_entry *e;

#ifdef __EMSCRIPTEN__
	return;	/* no threads: the reader loads the file itself */
#endif
	e = lex_cache_entry_get(path, len, LEX_CACHE_QUEUED, &ins);
	if (!ins)
		return;
	pthread_mutex_lock(&lex_prefetch_queue_mutex);
	if (lex_prefetch_queue_tail - lex_prefetch_queue_head == lex_prefetch_queue_capacity) {	/* grow, keeping FIFO order */
		size_t cap = lex_prefetch_queue_capacity ? 2 * lex_prefetch_queue_capacity : 4096;
		lex_cache_entry **q = malloc(cap * sizeof(*q));

		for (size_t i = 0; i < lex_prefetch_queue_tail - lex_prefetch_queue_head; i++)
			q[i] = lex_prefetch_queue[(lex_prefetch_queue_head + i) % lex_prefetch_queue_capacity];
		lex_prefetch_queue_tail -= lex_prefetch_queue_head;
		lex_prefetch_queue_head = 0;
		free(lex_prefetch_queue);
		lex_prefetch_queue = q;
		lex_prefetch_queue_capacity = cap;
	}
	lex_prefetch_queue[lex_prefetch_queue_tail++ % lex_prefetch_queue_capacity] = e;
	pthread_cond_signal(&lex_prefetch_queue_condition);
	pthread_mutex_unlock(&lex_prefetch_queue_mutex);
	pthread_once(&once, lex_prefetch_threads_start);
}
