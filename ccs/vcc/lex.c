/*
 * vcc lexer, see lex.h.
 */
#include <arm_neon.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "intern.h"
#include "lex.h"

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

static const char *const names[T_NKINDS] = {
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

const char *vtok_name(int kind)
{
	return kind >= 0 && kind < T_NKINDS ? names[kind] : "?";
}

/* ---- pass 1: NEON classification into bitmasks ---------------------- */

/*
 * Byte classes by nibble lookup: class = lo[c & 15] & hi[c >> 4].
 *   bit 0  hi 4,6  lo 1-f   A-O a-o
 *   bit 1  hi 5,7  lo 0-a   P-Z p-z
 *   bit 2  hi 3    lo 0-9   0-9
 *   bit 3  hi 5    lo f     _
 *   bit 4  hi 2    lo 4     $
 *   bit 5  hi 2    lo 0     space
 *   bit 6  hi 0    lo 9,b,c tab vt ff
 *   bit 7  hi 0    lo a,d   newline, carriage return
 */
static const uint8_t lo_tab[16] = {
	0x26, 0x07, 0x07, 0x07, 0x17, 0x07, 0x07, 0x07,
	0x07, 0x47, 0x83, 0x41, 0x41, 0x81, 0x01, 0x09,
};
static const uint8_t hi_tab[16] = {
	0xc0, 0x00, 0x30, 0x04, 0x01, 0x0a, 0x01, 0x02,
};

/*
 * Structural tables, indexed by byte (punctuation is ASCII):
 *   ttab[c]  bit 0  may double (&& || ++ -- ## :: << >>)
 *            bit 1  may take '=' (&= |= += -= *= ^= == != <= >= %= /=)
 *            bit 2  may take '>' (-> :> %>)
 *            bit 3  may take ':' or '%' (<: <%)
 *            bit 4  punctuator (not '.')
 *            bit 5  needs a handler: quotes, '\\', control, '@', '`'
 *            bit 6  '/': with '/' or '*' a comment
 *            bit 7  '%': with ':' or '%' a digraph we leave to the handlers
 *   gtab[c]  bit 1 '=', bit 2 '>', bits 3, 7 ':' or '%', bit 6 '/' or '*'
 */
static uint8_t ttab[128], gtab[64];

static void init_struct_tables(void)
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
		if (c && strchr("[](){}~?;,&|+-*^=!#:<>%/", c))
			t |= 16;
		if (c == '"' || c == '\'' || c == '\\' || c == '@' || c == '`' || c == 0x7f ||
		    (c < 0x20 && !strchr(" \t\v\f\n\r", c)) || c == 0)
			t |= 32;
		if (c == '/')
			t |= 64;
		if (c == '%')
			t |= 128;
		ttab[c] = t;
	}
	gtab['='] = 2;
	gtab['>'] = 4;
	gtab[':'] = gtab['%'] = 8 | 128;
	gtab['/'] = gtab['*'] = 64;
}

enum { M_ID, M_WS, M_NL, M_DOT, M_DIG, M_P, M_PAIR, M_X, M_ES, M_COUNT };

/*
 * 64 byte masks (0xff/0) to a 64-bit mask, for vectors loaded with vld4q:
 * a[k] lane j is byte 4j + k, which the shift-insert chain puts at bit
 * 4j + k.  Five instructions instead of the eight of a pairwise-add tree.
 */
static inline uint64_t mm4(uint8x16_t a0, uint8x16_t a1, uint8x16_t a2, uint8x16_t a3)
{
	uint8x16_t t0 = vsriq_n_u8(a1, a0, 1), t1 = vsriq_n_u8(a3, a2, 1);
	uint8x16_t t2 = vsriq_n_u8(t1, t0, 2), t3 = vsriq_n_u8(t2, t2, 4);

	return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(t3), 4)), 0);
}

/*
 * Per 64-byte block, bit i of each mask describes byte i:
 *   ID    identifier character       WS   whitespace incl. newlines
 *   NL    \n or \r                   DOT  '.'        DIG  digit
 *   P     punctuator character (not '.')
 *   PAIR  bytes i, i+1 form a two-character punctuator
 *   X     the structural pass stops here: literal, comment, splice, UCN,
 *         non-ASCII, NUL, stray character
 *   ES    e, E, p or P before + or - (a sign inside a pp-number)
 * Blocks [from, to) of the file at s.
 */
static void classify(const uint8_t *s, size_t from, size_t to, uint64_t *m[M_COUNT])
{
	const uint8x16_t lo = vld1q_u8(lo_tab), hi = vld1q_u8(hi_tab);
	const uint8x16_t nib = vdupq_n_u8(15), mid = vdupq_n_u8(0x1f), mdg = vdupq_n_u8(0x04);
	const uint8x16_t mws = vdupq_n_u8(0xe0), mnl = vdupq_n_u8(0x80);
	const uint8x16_t dot = vdupq_n_u8('.'), k64 = vdupq_n_u8(64);
	const uint8x16_t b1 = vdupq_n_u8(1), b16 = vdupq_n_u8(16), b32 = vdupq_n_u8(32);
	const uint8x16_t pairb = vdupq_n_u8(0x0e), cmtb = vdupq_n_u8(0xc0);

	for (size_t b = from; b < to; b++) {
		const uint8_t *q = s + 64 * b;
		const uint8x16x4_t v = vld4q_u8(q);
		uint8x16_t vn[4], t[4], tg[4], c[4], r[4];

		/* the next byte of each lane */
		vn[0] = v.val[1], vn[1] = v.val[2], vn[2] = v.val[3];
		vn[3] = vextq_u8(v.val[0], vld1q_dup_u8(q + 64), 1);
		{
			/*
			 * The 12 table registers are loaded per block (3 loads)
			 * instead of being live across the loop: that spilled
			 * and rematerialized ~60 instructions per block.
			 */
			const uint8_t *tp = ttab, *gp = gtab;

			__asm__("" : "+r"(tp), "+r"(gp));
			const uint8x16x4_t t0 = vld1q_u8_x4(tp), t1 = vld1q_u8_x4(tp + 64);

			for (int k = 0; k < 4; k++)
				t[k] = vorrq_u8(vqtbl4q_u8(t0, v.val[k]), vqtbl4q_u8(t1, vsubq_u8(v.val[k], k64)));
			const uint8x16x4_t g = vld1q_u8_x4(gp);

			for (int k = 0; k < 4; k++)
				tg[k] = vandq_u8(t[k], vqtbl4q_u8(g, vn[k]));
		}
		for (int k = 0; k < 4; k++)
			c[k] = vandq_u8(vqtbl1q_u8(lo, vandq_u8(v.val[k], nib)), vqtbl1q_u8(hi, vshrq_n_u8(v.val[k], 4)));
#define MASK(j, expr) do {						\
		for (int k = 0; k < 4; k++)					\
			r[k] = (expr);						\
		m[j][b] = mm4(r[0], r[1], r[2], r[3]);				\
	} while (0)
		MASK(M_P, vtstq_u8(t[k], b16));
		MASK(M_PAIR, vorrq_u8(vtstq_u8(tg[k], pairb), vandq_u8(vtstq_u8(t[k], b1), vceqq_u8(v.val[k], vn[k]))));
		MASK(M_X, vorrq_u8(vorrq_u8(vtstq_u8(t[k], b32), vtstq_u8(tg[k], cmtb)),
				   vreinterpretq_u8_s8(vshrq_n_s8(vreinterpretq_s8_u8(v.val[k]), 7))));
		MASK(M_ID, vtstq_u8(c[k], mid));
		MASK(M_WS, vtstq_u8(c[k], mws));
		MASK(M_NL, vtstq_u8(c[k], mnl));
		MASK(M_DIG, vtstq_u8(c[k], mdg));
		MASK(M_DOT, vceqq_u8(v.val[k], dot));
		MASK(M_ES, vandq_u8(vorrq_u8(vceqq_u8(vorrq_u8(v.val[k], vdupq_n_u8(0x20)), vdupq_n_u8('e')),
					     vceqq_u8(vorrq_u8(v.val[k], vdupq_n_u8(0x20)), vdupq_n_u8('p'))),
				    vorrq_u8(vceqq_u8(vn[k], vdupq_n_u8('+')), vceqq_u8(vn[k], vdupq_n_u8('-')))));
#undef MASK
	}
}

/* ---- NEON searches -------------------------------------------------- */

/* 4 bits per byte -> 64-bit mask; returns the byte index of the first hit */
static inline uint64_t nibmask(uint8x16_t eq)
{
	return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq), 4)), 0);
}

/* first position >= p holding '*' or NUL */
static inline size_t find_star(const char *s, size_t p)
{
	const uint8x16_t star = vdupq_n_u8('*');

	for (;; p += 16) {
		uint8x16_t v = vld1q_u8((const uint8_t *)s + p);
		uint64_t m = nibmask(vorrq_u8(vceqq_u8(v, star), vceqzq_u8(v)));

		if (m)
			return p + (__builtin_ctzll(m) >> 2);
	}
}

/* first position >= p holding '\n', '\r' or NUL */
static inline size_t find_nl(const char *s, size_t p)
{
	const uint8x16_t nl = vdupq_n_u8('\n'), cr = vdupq_n_u8('\r');

	for (;; p += 16) {
		uint8x16_t v = vld1q_u8((const uint8_t *)s + p);
		uint64_t m = nibmask(vorrq_u8(vorrq_u8(vceqq_u8(v, nl), vceqq_u8(v, cr)), vceqzq_u8(v)));

		if (m)
			return p + (__builtin_ctzll(m) >> 2);
	}
}

/* first position >= p holding q, a backslash or a byte <= '\r' */
static inline size_t find_quote(const char *s, size_t p, uint8_t q)
{
	const uint8x16_t vq = vdupq_n_u8(q), bs = vdupq_n_u8('\\'), cr = vdupq_n_u8('\r');

	for (;; p += 16) {
		uint8x16_t v = vld1q_u8((const uint8_t *)s + p);
		uint8x16_t hit = vorrq_u8(vorrq_u8(vceqq_u8(v, vq), vceqq_u8(v, bs)), vcleq_u8(v, cr));
		uint64_t m = nibmask(hit);

		if (m)
			return p + (__builtin_ctzll(m) >> 2);
	}
}

/* ---- pass 2: tokens ------------------------------------------------- */

enum {
	C_OTHER, C_WS, C_NL, C_CR, C_NUL, C_ID, C_PFX, C_DIGIT, C_PUNCT,
	C_SQUOTE, C_DQUOTE, C_BSLASH, C_HIGH,
};

static uint8_t cls[256];
static uint8_t isid[256];

/* fast path classes and punctuator table */
enum { K_ID, K_NUM, K_PUNCT, K_SLOW };
enum { PT_ALT = 1 << 11, PT_SLOW = 1 << 12 };
static uint8_t fcls[256];
static uint8_t pidx[256];		/* row in ptab, 0 = not a punctuator */
static uint16_t ptab[27][256];		/* kind | len << 8 | PT_* */
static struct { uint8_t c, kind, len; } ptab_alt[T_NKINDS];
static uint8_t ktab[1 << 16];		/* punctuator kind by its first two bytes (LE) */

static void init_tables(void)
{
	for (int c = 0; c < 256; c++) {
		uint8_t k = C_OTHER;

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$')
			k = C_ID;
		if (c == 'L' || c == 'u' || c == 'U' || c == 'R')
			k = C_PFX;
		if (c >= '0' && c <= '9')
			k = C_DIGIT;
		if (c >= 0x80)
			k = C_HIGH;
		if (c && strchr("[](){}.&*+-~!/%<>^|?:;=,#", c))
			k = C_PUNCT;
		isid[c] = k == C_ID || k == C_PFX || k == C_DIGIT;
		cls[c] = k;
	}
	cls[' '] = cls['\t'] = cls['\v'] = cls['\f'] = C_WS;
	cls['\n'] = C_NL;
	cls['\r'] = C_CR;
	cls[0] = C_NUL;
	cls['\''] = C_SQUOTE;
	cls['"'] = C_DQUOTE;
	cls['\\'] = C_BSLASH;

	static const char pch[] = "[](){}~?;,.&|+-*^=!#:<>%/";
	static const struct { char a, b; uint8_t kind; } two[] = {
		{ '&', '&', T_AMPAMP }, { '&', '=', T_AMPEQ }, { '|', '|', T_PIPEPIPE },
		{ '|', '=', T_PIPEEQ }, { '+', '+', T_PLUSPLUS }, { '+', '=', T_PLUSEQ },
		{ '-', '>', T_ARROW }, { '-', '-', T_MINUSMINUS }, { '-', '=', T_MINUSEQ },
		{ '*', '=', T_STAREQ }, { '^', '=', T_CARETEQ }, { '=', '=', T_EQUALEQUAL },
		{ '!', '=', T_EXCLAIMEQ }, { '#', '#', T_HASHHASH }, { ':', ':', T_COLONCOLON },
		{ ':', '>', T_RSQUARE }, { '<', '<', T_LESSLESS }, { '<', '=', T_LESSEQ },
		{ '<', ':', T_LSQUARE }, { '<', '%', T_LBRACE }, { '>', '>', T_GREATERGREATER },
		{ '>', '=', T_GREATEREQ }, { '%', '=', T_PERCENTEQ }, { '%', '>', T_RBRACE },
		{ '/', '=', T_SLASHEQ },
	};
	static const uint8_t one[] = {
		T_LSQUARE, T_RSQUARE, T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_TILDE,
		T_QUESTION, T_SEMI, T_COMMA, T_PERIOD, T_AMP, T_PIPE, T_PLUS, T_MINUS,
		T_STAR, T_CARET, T_EQUAL, T_EXCLAIM, T_HASH, T_COLON, T_LESS, T_GREATER,
		T_PERCENT, T_SLASH,
	};

	for (int c = 0; c < 256; c++)
		fcls[c] = K_SLOW;
	for (int c = 0; c < 256; c++)
		if (isid[c])
			fcls[c] = c >= '0' && c <= '9' ? K_NUM : K_ID;
	for (int i = 0; pch[i]; i++) {
		uint8_t c = (uint8_t)pch[i];

		fcls[c] = K_PUNCT;
		pidx[c] = (uint8_t)(i + 1);
		for (int d = 0; d < 256; d++)
			ptab[i + 1][d] = (uint16_t)(one[i] | 1 << 8);
		ptab[i + 1]['\\'] |= PT_SLOW;	/* maybe a splice */
	}
	for (unsigned i = 0; i < sizeof(two) / sizeof(two[0]); i++)
		ptab[pidx[(uint8_t)two[i].a]][(uint8_t)two[i].b] = (uint16_t)(two[i].kind | 2 << 8);
	/* three-character forms: decided by the third character */
	ptab[pidx['<']]['<'] |= PT_ALT;
	ptab_alt[T_LESSLESS].c = '=', ptab_alt[T_LESSLESS].kind = T_LESSLESSEQ, ptab_alt[T_LESSLESS].len = 3;
	ptab[pidx['>']]['>'] |= PT_ALT;
	ptab_alt[T_GREATERGREATER].c = '=', ptab_alt[T_GREATERGREATER].kind = T_GREATERGREATEREQ,
		ptab_alt[T_GREATERGREATER].len = 3;
	ptab[pidx['.']]['.'] |= PT_ALT;		/* ".." is '.' unless "..." */
	ptab_alt[T_PERIOD].c = '.', ptab_alt[T_PERIOD].kind = T_ELLIPSIS, ptab_alt[T_PERIOD].len = 3;
	/* to the handlers: comments, ".5", "%:" (maybe "%:%:") */
	ptab[pidx['/']]['*'] |= PT_SLOW;
	ptab[pidx['/']]['/'] |= PT_SLOW;
	ptab[pidx['%']][':'] |= PT_SLOW;
	for (int d = '0'; d <= '9'; d++)
		ptab[pidx['.']][d] |= PT_SLOW;
	for (int c = 0; c < 256; c++)
		for (int d = 0; d < 256; d++)
			ktab[c | d << 8] = (uint8_t)(ptab[pidx[c]][d] & 0xff);
	/*
	 * The structural loop takes every token's kind from ktab: a token
	 * starting with an identifier character is an identifier or (digit) a
	 * number, ".5" a number, and ".." an ellipsis (a lone ".." goes to
	 * the handlers).
	 */
	for (int c = 0; c < 256; c++)
		for (int d = 0; d < 256; d++) {
			if (isid[c])
				ktab[c | d << 8] = c >= '0' && c <= '9' ? T_NUMBER : T_IDENT;
			if (c == '.' && d >= '0' && d <= '9')
				ktab[c | d << 8] = T_NUMBER;
		}
	ktab['.' | '.' << 8] = T_ELLIPSIS;
}

/* length of the line splice ("\\", horizontal space, newline) at p, or 0 */
static inline size_t splice_len(const char *s, size_t p)
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

static inline size_t skip_splices(const char *s, size_t p)
{
	size_t n;

	while ((n = splice_len(s, p)))
		p += n;
	return p;
}

/* position of the logical character after the one at p */
static inline size_t nx(const char *s, size_t p)
{
	p++;
	if (unlikely(s[p] == '\\'))
		p = skip_splices(s, p);
	return p;
}

static size_t utf8_len(const uint8_t *s);

/* C11 annex D: characters allowed in identifiers */
static bool c11_id(uint32_t c, bool first)
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

static inline int hexval(uint8_t c)
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
static size_t ext_id(const char *s, size_t p, bool first)
{
	const uint8_t *u = (const uint8_t *)s + p;
	uint32_t c = 0;
	size_t n;

	if (u[0] == '\\') {
		n = u[1] == 'u' ? 4 : u[1] == 'U' ? 8 : 0;
		if (!n)
			return 0;
		for (size_t i = 0; i < n; i++) {
			int h = hexval(u[2 + i]);

			if (h < 0)
				return 0;
			c = c << 4 | (uint32_t)h;
		}
		n += 2;
	} else if (u[0] >= 0x80) {
		n = utf8_len(u);
		if (n == 1)
			return 0;
		c = n == 2 ? u[0] & 0x1f : n == 3 ? u[0] & 0x0f : u[0] & 0x07;
		for (size_t i = 1; i < n; i++)
			c = c << 6 | (u[i] & 0x3f);
	} else {
		return 0;
	}
	return c11_id(c, first) ? n : 0;
}

static inline bool is_exp(char c)
{
	return c == 'e' || c == 'E' || c == 'p' || c == 'P';
}

/* end of a pp-number whose first character ends before p */
static inline size_t idrun(const char *s, size_t p)
{
	while (isid[(uint8_t)s[p]])
		p++;
	return p;
}

static size_t number_end(const char *s, size_t p, unsigned *fl)
{
	for (;;) {
		char c;

		p = idrun(s, p);
		c = s[p];
		if (c == '.' || ((c == '+' || c == '-') && is_exp(s[p - 1]))) {
			p++;
			continue;
		}
		if (unlikely((uint8_t)c >= 0x80 || (c == '\\' && ext_id(s, p, false)))) {
			size_t l = ext_id(s, p, false);

			if (l) {
				p += l;
				continue;
			}
		}
		if (unlikely(c == '\\')) {
			size_t q = skip_splices(s, p);
			char d = s[q];

			size_t l;

			if (q != p && (isid[(uint8_t)d] || d == '.' ||
				       ((d == '+' || d == '-') && is_exp(s[p - 1])))) {
				*fl |= TF_DIRTY;
				p = q + 1;
				continue;
			}
			if (q != p && (l = ext_id(s, q, false))) {
				*fl |= TF_DIRTY;
				p = q + l;
				continue;
			}
		}
		return p;
	}
}

static inline size_t ident_end(const char *s, size_t p, unsigned *fl)
{
	p = idrun(s, p);
	while (unlikely(s[p] == '\\' || (uint8_t)s[p] >= 0x80)) {
		size_t q = skip_splices(s, p), l;

		if (q != p && isid[(uint8_t)s[q]]) {
			*fl |= TF_DIRTY;
			p = idrun(s, q);
		} else if ((l = ext_id(s, q, false))) {
			if (q != p)
				*fl |= TF_DIRTY;
			p = idrun(s, q + l);
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
static size_t quoted_end(const char *s, size_t n, size_t p, uint8_t q, unsigned *fl, size_t *bad)
{
	for (p++;;) {
		uint8_t c;

		p = find_quote(s, p, q);
		c = s[p];
		if (c == q)
			return p + 1;
		if (c == '\\') {
			size_t l = splice_len(s, p);

			if (l) {
				*fl |= TF_DIRTY;
				p += l;
				continue;
			}
			/* escape: skip the next logical character */
			l = skip_splices(s, p + 1);
			if (l != p + 1)
				*fl |= TF_DIRTY;
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

static inline bool raw_delim_char(uint8_t c)
{
	return c > ' ' && c < 0x7f && c != '(' && c != ')' && c != '\\';
}

/* raw string literal; p is at the '"'.  0 if invalid (*bad = end) */
static size_t raw_end(const char *s, size_t n, size_t p, size_t *bad)
{
	size_t d = p + 1, dl = 0, i;

	while (dl < 16 && raw_delim_char(s[d + dl]))
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
static size_t utf8_len(const uint8_t *s)
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

size_t vlex_scratch_size(size_t size)
{
	/* four masks (with two spare words) and the identifier list */
	return M_COUNT * (size / 64 + 3) * sizeof(uint64_t) + (size + 2) * sizeof(uint32_t);
}

_Thread_local int vlex_no_intern;
static pthread_once_t tables_once = PTHREAD_ONCE_INIT;

/* 64 mask bits starting at position p (m[(p >> 6) + 1] must exist) */
static inline uint64_t win(const uint64_t *m, size_t p)
{
	size_t w = p >> 6;
	unsigned b = p & 63;

	return (m[w] >> b) | ((m[w + 1] << 1) << (63 - b));
}

/*
 * Literal with an encoding prefix: the identifier-like prefix is st..p and
 * a quote (perhaps after splices) follows.  Returns the end of the literal
 * and sets *kind, or 0 if st..p is not a prefix (it is an identifier).
 */
static __attribute__((noinline)) size_t prefixed(const char *s, size_t n, size_t st, size_t p,
						 unsigned *fl, unsigned *kind)
{
	static const struct { char s[4]; uint8_t sq, dq; } pf[] = {
		{ "L", T_WCHAR, T_WSTRING }, { "u", T_U16CHAR, T_U16STRING },
		{ "U", T_U32CHAR, T_U32STRING }, { "u8", 0, T_U8STRING },
		{ "R", 0, T_STRING }, { "LR", 0, T_WSTRING },
		{ "uR", 0, T_U16STRING }, { "UR", 0, T_U32STRING },
		{ "u8R", 0, T_U8STRING },
	};
	size_t q = s[p] == '\\' ? skip_splices(s, p) : p, l = 0, a, b, bad;
	uint8_t qc = (uint8_t)s[q];
	char pre[8];

	if (qc != '"' && qc != '\'')
		return 0;
	if (p - st <= 3 && !(*fl & TF_DIRTY)) {
		memcpy(pre, s + st, p - st);
		l = p - st;
	} else if (p - st < 64) {
		char tmp[64];
		vtok tt = { .off = (uint32_t)st, .len = (uint32_t)(p - st), .flags = TF_DIRTY };

		l = vlex_spell(s, &tt, tmp);
		if (l <= 3)
			memcpy(pre, tmp, l);
		else
			l = 0;
	}
	for (unsigned i = 0; l && i < sizeof(pf) / sizeof(pf[0]); i++) {
		if (strlen(pf[i].s) != l || memcmp(pf[i].s, pre, l))
			continue;
		if (qc == '"' && pre[l - 1] == 'R') {
			a = raw_end(s, n, q, &bad);
			*kind = pf[i].dq;
			if (a)
				*fl |= TF_RAW;
		} else if (qc == '"') {
			a = quoted_end(s, n, q, '"', fl, &bad);
			*kind = pf[i].dq;
		} else if (pf[i].sq) {
			b = nx(s, q);
			if (s[b] == '\'') {
				a = 0;
				bad = b + 1;
			} else {
				a = quoted_end(s, n, q, '\'', fl, &bad);
			}
			*kind = pf[i].sq;
		} else {
			return 0;
		}
		if (q != p)
			*fl |= TF_DIRTY;
		if (!a) {
			*kind = T_UNKNOWN;
			a = bad;
		}
		return a;
	}
	return 0;
}

/* an identifier with a splice in it: intern its cleaned spelling */
static __attribute__((noinline)) uint32_t intern_dirty(const char *s, size_t st, size_t p)
{
	char tmp[4096];
	vtok tt = { .off = (uint32_t)st, .len = (uint32_t)(p - st), .flags = TF_DIRTY };
	size_t l = p - st < sizeof(tmp) ? vlex_spell(s, &tt, tmp) : 0;

	return vintern_copy(tmp, l);
}

/* intern the identifiers out[il[0..ni)] (a loop of its own: registers) */
static __attribute__((noinline)) void intern_list(vintern_cache *ic, const char *s, vtok *out,
						  const uint32_t *il, uint32_t ni)
{
	for (uint32_t i = 0; i < ni; i++) {
		vtok *k = &out[il[i]];

		k->id = vintern_fast(ic, s + k->off, k->len);
	}
}

/*
 * Classify blocks [from, to) clamped to the file's nblk blocks; the block
 * holding the end gets nothing but X past it.  Returns the new end.
 */
static size_t classify_upto(const char *s, size_t n, size_t nblk, size_t from, size_t to,
			    uint64_t *m[M_COUNT])
{
	if (to > nblk)
		to = nblk;
	classify((const uint8_t *)s, from, to, m);
	if (from <= (n >> 6) && (n >> 6) < to) {
		for (int j = 0; j < M_COUNT; j++)
			if (j != M_X)
				m[j][n >> 6] &= ~(~0ull << (n & 63));
		m[M_X][n >> 6] |= ~0ull << (n & 63);
	}
	return to == nblk ? (size_t)-1 >> 1 : to;
}

/* a token with a splice before its first character starts at the splice */
#define FLAGS() ((uint8_t)(fl | (gap != st ? TF_DIRTY : 0)))

#define DISPATCH() do { STAT((uint8_t)s[p]); goto *disp[(uint8_t)s[st = p]]; } while (0)

#ifdef VLEX_STATS
unsigned long vlex_stat[512];
#define STAT(i) (vlex_stat[i]++)
#define STAT_T0() uint64_t t0_ = __builtin_readcyclecounter()
#define STAT_T1(i) (vlex_stat[i] += __builtin_readcyclecounter() - t0_)
__attribute__((destructor)) static void stat_dump(void)
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
		fl |= TF_DIRTY;						\
	EMIT(K);							\
} while (0)

/* identifier body: the first character ends before p */
#define IDENT_REST() do {						\
	p = idrun(s, p);						\
	if (unlikely(s[p] == '\\' || (uint8_t)s[p] >= 0x80))		\
		p = ident_end(s, p, &fl);				\
} while (0)

/* clean identifiers are interned after the token loop, see il[] */
#define EMIT_IDENT() do {						\
	uint32_t i_ = 0;						\
	if (unlikely(vlex_no_intern))					\
		;							\
	else if (fl & TF_DIRTY)						\
		i_ = intern_dirty(s, st, p);				\
	else if (gap == st)						\
		il[ni++] = (uint32_t)(t - out);				\
	else								\
		i_ = vintern_fast(ic, s + st, p - st);			\
	t->off = (uint32_t)gap;						\
	t->len = (uint32_t)(p - gap);					\
	t->id = i_;							\
	t->kind = T_IDENT;						\
	t->flags = FLAGS();						\
	t++;								\
	fl = 0;								\
	gap = p;							\
	NEXT();								\
} while (0)

uint32_t vlex(const char *s, size_t n, vtok *out, void *scratch)
{
	static const void *disp[256];
	size_t nb = n / 64 + 3, nblk = n / 64 + 1, p = 0, st = 0, q, a, b, bad;
	uint64_t *m[M_COUNT];
	uint32_t *il = (uint32_t *)((uint64_t *)scratch + M_COUNT * nb), ni = 0;
	unsigned fl = TF_SOL, kind;
	vintern_cache *ic = vintern_local();
	vtok *t = out;
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
			pthread_once(&tables_once, init_tables);
			init_struct_tables();
			for (int c = 1; c < 256; c++)
				disp[c] = c >= 0x80 ? &&l_high : isid[c] ? &&l_ident : &&l_other;
			for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
				for (const char *c = map[i].chars; *c; c++)
					disp[(uint8_t)*c] = map[i].label;
			atomic_store_explicit((_Atomic(const void *) *)&disp[0], &&l_nul, memory_order_release);
		}
		pthread_mutex_unlock(&lock);
	}

	for (int i = 0; i < M_COUNT; i++)
		m[i] = (uint64_t *)scratch + i * nb;
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
		cl_end = classify_upto(s, n, nblk, f_, f_ + CHUNK, m);	\
	}								\
} while (0)

	if (unlikely(vlex_no_intern == 2)) {
		classify_upto(s, n, nblk, 0, nb, m);
		return 0;
	}
	for (size_t i = nblk; i < nb; i++)
		for (int j = 0; j < M_COUNT; j++)
			m[j][i] = j == M_X ? ~0ull : 0;

	/*
	 * Structural pass over 64-byte windows: every token start in the
	 * window is computed at once from the masks, so consecutive tokens do
	 * not depend on each other and no branch depends on the token kind.
	 * At an X position (literal, comment, splice, ...) the handlers above
	 * take over for a token and come back here.
	 */
fast:
	if (unlikely(gap != p))
		DISPATCH();	/* a token directly after a splice */
	ENSURE((p >> 6) + 1);
	{
		const uint64_t I = win(m[M_ID], p), W = win(m[M_WS], p), NL = win(m[M_NL], p);
		const uint64_t DOT = win(m[M_DOT], p), D = win(m[M_DIG], p), P = win(m[M_P], p);
		const uint64_t PR = win(m[M_PAIR], p);
		uint64_t X = win(m[M_X], p);
		/* pp-numbers: from a digit starting a run of [A-Za-z0-9_$.], or
		 * from a '.' before a digit, to the end of that run */
		const uint64_t N = I | DOT, A = (D & ~(N << 1)) | (DOT & (D >> 1));
		const uint64_t NUM = (N & ~(N + A)) | A;
		const uint64_t IDp = I & ~NUM, DOTp = DOT & ~NUM;
		const uint64_t ELL = DOTp & (DOTp >> 1) & (DOTp >> 2);
		/* left to the handlers: "..5", "....", overlapping pairs (<<=) */
		const uint64_t DD = DOT & (D >> 1) & (DOT << 1);	/* ...5: from the first dot */
		const uint64_t ES = win(m[M_ES], p);

		/* and 1e+5: the sign belongs to the number */
		X |= DD | (DD >> 1) | (DD >> 2) | (ELL & ((ELL >> 1) | (ELL >> 2))) | (PR & (PR >> 1)) |
		     (ES & NUM) | (DOTp & (DOTp >> 1) & ~ELL & ~(ELL << 1));
		X |= (X >> 2) & DOT & (DOT >> 1);	/* "..\<newline>." */
		/* ".." in the last two bits: a third dot would be past the window */
		X |= DOT & (DOT >> 1) & (1ull << 62);

		const uint64_t IDs = IDp & ~(IDp << 1), NUMs = NUM & ~(NUM << 1);
		const uint64_t S = IDs | NUMs | ((P | DOTp) & ~((PR << 1) | (ELL << 1) | (ELL << 2)));
		const uint64_t B = S | W | X;
		/* whitespace runs that contain a newline, filled to their end */
		const uint64_t NLF = (W & ~(W + NL)) | NL;
		const uint64_t SP = W << 1, SOL = NLF << 1;
		const unsigned lim = X ? (unsigned)__builtin_ctzll(X) : 64;
		uint64_t todo = S;
		/*
		 * the last byte of every token: a non-blank byte before a
		 * boundary.  Below lim they pair up with the starts in todo.
		 */
		uint64_t ends = ~W & ((B >> 1) | 1ull << 63);
		unsigned last = 0, sflags = 0, pend = fl;
		bool any = false;

		while (todo) {
			const unsigned o = (unsigned)__builtin_ctzll(todo);
			const unsigned e = 1 + (unsigned)__builtin_ctzll(ends);
			const size_t at = p + o;
			const unsigned f = ((SP >> o) & 1) * TF_SPACE | ((SOL >> o) & 1) * TF_SOL;
			uint16_t c01;

			sflags = f;
			if (e >= lim)
				break;	/* reaches X or past the window */
			memcpy(&c01, s + at, 2);
			/* kind without branches: ktab knows identifiers and numbers too */
			const unsigned kind = ktab[c01];
			t->off = (uint32_t)at;
			t->len = e - o;
			t->id = 0;
			t->kind = (uint8_t)kind;
			t->flags = (uint8_t)(f | pend);
			pend = 0;
			il[ni] = (uint32_t)(t - out);
			ni += kind == T_IDENT;
			t++;
			last = e;
			any = true;
			todo &= todo - 1;
			ends &= ends - 1;
		}
		fl = pend;
		if (likely(any)) {
			/*
			 * Only whitespace up to the X: take it and go to the
			 * handler, instead of a window that finds just that.
			 */
			if (lim < 64 && !(~W & (~0ull << last) & ~(~0ull << lim))) {
				if (lim > last)
					fl |= TF_SPACE | (NL & (~0ull << last) & ~(~0ull << lim) ? TF_SOL : 0);
				p = gap = p + lim;
				DISPATCH();
			}
			p = gap = p + last;
			goto fast;
		}
		if (todo && (unsigned)__builtin_ctzll(todo) < lim) {
			/* the first token needs a handler (or is longer than the window) */
			const unsigned o = (unsigned)__builtin_ctzll(todo);

			fl |= sflags;
			p = gap = p + o;
			DISPATCH();
		}
		/* whitespace, then an X or the next window */
		{
			const unsigned o = ~W ? (unsigned)__builtin_ctzll(~W) : 64;

			if (o) {
				fl |= TF_SPACE | (NL & (o == 64 ? ~0ull : (1ull << o) - 1) ? TF_SOL : 0);
				p = gap = p + o;
			}
			if (o == 64)
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
	    (q = prefixed(s, n, st, p, &fl, &kind))) {
		p = q;
		EMIT(kind);
	}
	EMIT_IDENT();
l_digit:
	p = number_end(s, p + 1, &fl);
	EMIT(T_NUMBER);
l_space:	/* only after a NUL or a splice the masks did not cover */
	fl |= TF_SPACE | (s[p] == '\n' || s[p] == '\r' ? TF_SOL : 0);
	gap = ++p;
	NEXT();
l_nul:
	if (p >= n)
		goto done;
	gap = ++p;
	fl |= TF_SPACE;
	NEXT();
l_bslash:
	if ((q = splice_len(s, p))) {
		p += q;
		NEXT();
	}
	if ((q = ext_id(s, p, true))) {
		p = ident_end(s, p + q, &fl);
		EMIT_IDENT();
	}
	/* a well-formed UCN that may not start an identifier */
	q = s[p + 1] == 'u' ? 4 : s[p + 1] == 'U' ? 8 : 0;
	for (a = 0; a < q && hexval((uint8_t)s[p + 2 + a]) >= 0; a++)
		;
	p += q && a == q ? q + 2 : 1;
	EMIT(T_UNKNOWN);
l_dquote:
	q = quoted_end(s, n, p, '"', &fl, &bad);
	kind = T_STRING;
	goto lit;
l_squote:
	if (s[a = nx(s, p)] == '\'') {
		q = 0;
		bad = a + 1;
		if (a != p + 1)
			fl |= TF_DIRTY;
	} else {
		q = quoted_end(s, n, p, '\'', &fl, &bad);
	}
	kind = T_CHAR;
lit:
	if (q) {
		p = q;
		EMIT(kind);
	}
	p = bad;
	EMIT(T_UNKNOWN);
l_high:
	if ((q = ext_id(s, p, true))) {
		p = ident_end(s, p + q, &fl);
		EMIT_IDENT();
	}
	p += utf8_len((const uint8_t *)s + p);
	EMIT(T_UNKNOWN);
l_other:
	p++;
	EMIT(T_UNKNOWN);

l_lsquare:	PUNCT(1, T_LSQUARE, p + 1);
l_rsquare:	PUNCT(1, T_RSQUARE, p + 1);
l_lparen:	PUNCT(1, T_LPAREN, p + 1);
l_rparen:	PUNCT(1, T_RPAREN, p + 1);
l_lbrace:	PUNCT(1, T_LBRACE, p + 1);
l_rbrace:	PUNCT(1, T_RBRACE, p + 1);
l_tilde:	PUNCT(1, T_TILDE, p + 1);
l_question:	PUNCT(1, T_QUESTION, p + 1);
l_semi:		PUNCT(1, T_SEMI, p + 1);
l_comma:	PUNCT(1, T_COMMA, p + 1);
l_period:
	a = nx(s, p);
	if (s[a] >= '0' && s[a] <= '9') {
		if (a != p + 1)
			fl |= TF_DIRTY;
		p = number_end(s, a + 1, &fl);
		EMIT(T_NUMBER);
	}
	if (s[a] == '.' && s[b = nx(s, a)] == '.')
		PUNCT(3, T_ELLIPSIS, b + 1);
	PUNCT(1, T_PERIOD, p + 1);
l_amp:
	a = nx(s, p);
	if (s[a] == '&') PUNCT(2, T_AMPAMP, a + 1);
	if (s[a] == '=') PUNCT(2, T_AMPEQ, a + 1);
	PUNCT(1, T_AMP, p + 1);
l_pipe:
	a = nx(s, p);
	if (s[a] == '|') PUNCT(2, T_PIPEPIPE, a + 1);
	if (s[a] == '=') PUNCT(2, T_PIPEEQ, a + 1);
	PUNCT(1, T_PIPE, p + 1);
l_plus:
	a = nx(s, p);
	if (s[a] == '+') PUNCT(2, T_PLUSPLUS, a + 1);
	if (s[a] == '=') PUNCT(2, T_PLUSEQ, a + 1);
	PUNCT(1, T_PLUS, p + 1);
l_minus:
	a = nx(s, p);
	if (s[a] == '>') PUNCT(2, T_ARROW, a + 1);
	if (s[a] == '-') PUNCT(2, T_MINUSMINUS, a + 1);
	if (s[a] == '=') PUNCT(2, T_MINUSEQ, a + 1);
	PUNCT(1, T_MINUS, p + 1);
l_star:
	a = nx(s, p);
	if (s[a] == '=') PUNCT(2, T_STAREQ, a + 1);
	PUNCT(1, T_STAR, p + 1);
l_caret:
	a = nx(s, p);
	if (s[a] == '=') PUNCT(2, T_CARETEQ, a + 1);
	PUNCT(1, T_CARET, p + 1);
l_equal:
	a = nx(s, p);
	if (s[a] == '=') PUNCT(2, T_EQUALEQUAL, a + 1);
	PUNCT(1, T_EQUAL, p + 1);
l_exclaim:
	a = nx(s, p);
	if (s[a] == '=') PUNCT(2, T_EXCLAIMEQ, a + 1);
	PUNCT(1, T_EXCLAIM, p + 1);
l_hash:
	a = nx(s, p);
	if (s[a] == '#') PUNCT(2, T_HASHHASH, a + 1);
	PUNCT(1, T_HASH, p + 1);
l_colon:
	a = nx(s, p);
	if (s[a] == ':') PUNCT(2, T_COLONCOLON, a + 1);
	if (s[a] == '>') PUNCT(2, T_RSQUARE, a + 1);
	PUNCT(1, T_COLON, p + 1);
l_less:
	a = nx(s, p);
	if (s[a] == '<') {
		if (s[b = nx(s, a)] == '=') PUNCT(3, T_LESSLESSEQ, b + 1);
		PUNCT(2, T_LESSLESS, a + 1);
	}
	if (s[a] == '=') PUNCT(2, T_LESSEQ, a + 1);
	if (s[a] == ':') PUNCT(2, T_LSQUARE, a + 1);
	if (s[a] == '%') PUNCT(2, T_LBRACE, a + 1);
	PUNCT(1, T_LESS, p + 1);
l_greater:
	a = nx(s, p);
	if (s[a] == '>') {
		if (s[b = nx(s, a)] == '=') PUNCT(3, T_GREATERGREATEREQ, b + 1);
		PUNCT(2, T_GREATERGREATER, a + 1);
	}
	if (s[a] == '=') PUNCT(2, T_GREATEREQ, a + 1);
	PUNCT(1, T_GREATER, p + 1);
l_percent:
	a = nx(s, p);
	if (s[a] == '=') PUNCT(2, T_PERCENTEQ, a + 1);
	if (s[a] == '>') PUNCT(2, T_RBRACE, a + 1);
	if (s[a] == ':') {
		b = nx(s, a);
		if (s[b] == '%' && s[q = nx(s, b)] == ':') PUNCT(4, T_HASHHASH, q + 1);
		PUNCT(2, T_HASH, a + 1);
	}
	PUNCT(1, T_PERCENT, p + 1);
l_slash:
	a = nx(s, p);
	if (s[a] == '*') {
		/* block comment */
		for (q = a + 1;; q++) {
			q = find_star(s, q);
			if (q >= n) {
				/* unterminated: an error token */
				p = n;
				EMIT(T_UNKNOWN);
			}
			if (s[q] != '*')
				continue;	/* embedded NUL */
			b = nx(s, q);
			if (s[b] == '/') {
				q = b + 1;
				break;
			}
		}
		p = gap = q;
		fl |= TF_SPACE;
		NEXT();
	}
	if (s[a] == '/') {
		/* line comment: up to an unspliced \n or \r */
		for (q = a + 1;;) {
			b = find_nl(s, q);
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
			if (e == q || s[e - 1] != '\\' || !splice_len(s, e - 1))
				break;
			q = b + 1;
			/* skip the other half of this splice's newline */
			if ((s[q] == '\n' || s[q] == '\r') && s[q] != s[b])
				q++;
		}
		p = gap = b < n ? b : n;
		fl |= TF_SPACE;
		NEXT();
	}
	if (s[a] == '=') PUNCT(2, T_SLASHEQ, a + 1);
	PUNCT(1, T_SLASH, p + 1);

done:
	t->off = (uint32_t)n;
	t->len = 0;
	t->id = 0;
	t->kind = T_EOF;
	t->flags = (uint8_t)fl;
	/* intern the clean identifiers: no unpredictable branches left here */
	STAT_T0();
	if (!vlex_no_intern)
		intern_list(ic, s, out, il, ni);
	STAT_T1(320);
	return (uint32_t)(t - out);
}

size_t vlex_spell(const char *s, const vtok *t, char *buf)
{
	size_t p = t->off, e = t->off + t->len, n = 0;

	if (!(t->flags & TF_DIRTY)) {
		memcpy(buf, s + p, t->len);
		return t->len;
	}
	while (p < e) {
		size_t l = splice_len(s, p);

		if (l) {
			p += l;
			continue;
		}
		if ((t->flags & TF_RAW) && s[p] == '"') {
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
static _Thread_local char *ar_cur, *ar_end;

static void *arena(size_t n)
{
	char *p;

	n = (n + 63) & ~(size_t)63;
	if ((size_t)(ar_end - ar_cur) < n) {
		size_t sz = n > (64u << 20) ? n : (64u << 20);

		p = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (p == MAP_FAILED) {
			perror("vcc: mmap");
			abort();
		}
		ar_cur = p;
		ar_end = p + sz;
	}
	p = ar_cur;
	ar_cur += n;
	return p;
}

/* give back the unused tail of the last arena() allocation */
static void arena_trim(void *p, size_t used)
{
	ar_cur = (char *)p + ((used + 63) & ~(size_t)63);
}

static _Thread_local void *scratch;
static _Thread_local size_t scratch_size;

/*
 * The file's bytes, read into the arena with 128 zero bytes after them (the
 * lexer reads ahead).  read() beats mmap here: no mapping per file, no
 * second mapping for files whose last page has no slack, fewer faults.
 * Returns the bytes actually read (the file may have shrunk meanwhile).
 */
static char *read_file(int fd, size_t size, size_t *got)
{
	char *b = arena(size + 128);	/* not always fresh: arena_trim gives tails back */
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
	*got = n;
	return b;
}

/* open + read: the file's bytes in this thread's arena, false if unreadable */
static bool read_src(const char *path, const char **src, size_t *size)
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
	*src = read_file(fd, (size_t)st.st_size, size);
	close(fd);
	return true;
}

/* lex src (from read_src) into f, tokens in this thread's arena */
static void lex_src(vlex_file *f, const char *src, size_t size)
{
	size_t need = vlex_scratch_size(size);
	vtok *tok;

	if (need > scratch_size) {
		if (scratch)
			munmap(scratch, scratch_size);
		scratch_size = need < (1u << 20) ? (1u << 20) : need * 2;
		scratch = mmap(NULL, scratch_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (scratch == MAP_FAILED)
			abort();
	}
	tok = arena((size + 2) * sizeof(vtok));
	f->src = src;
	f->size = (uint32_t)size;
	f->ntok = vlex(src, size, tok, scratch);
	f->tok = tok;
	arena_trim(tok, (f->ntok + 1) * sizeof(vtok));
}

static bool load(vlex_file *f, const char *path)
{
	const char *src;
	size_t size;

	if (!read_src(path, &src, &size))
		return false;
	lex_src(f, src, size);
	return true;
}

/* benchmarking: lex path as vlex_get() would, without the cache (leaks) */
int vlex_load_uncached(const char *path, vlex_file *f)
{
	return load(f, path);
}

/*
 * Cache entry states.  QUEUED: waiting for an I/O thread (vlex_prefetch);
 * READING: an I/O thread has it; READ: bytes in f.src, not lexed; LOADING:
 * a lexing thread has it; OK / MISSING: final.
 */
enum { CE_QUEUED, CE_READING, CE_READ, CE_LOADING, CE_OK, CE_MISSING };

typedef struct centry {
	vlex_file f;
	uint32_t hash, len;
	_Atomic int state;
	char path[];
} centry;

#define CBITS 16
static _Atomic(centry *) ctab[1u << CBITS];

/* the entry for path, inserted in state st0 if absent (*ins set then) */
static centry *centry_get(const char *path, size_t len, int st0, bool *ins)
{
	uint32_t h = vintern_hash(path, len);
	centry *mine = NULL;

	*ins = false;
	for (uint32_t i = h >> (32 - CBITS);; i = (i + 1) & ((1u << CBITS) - 1)) {
		centry *e = atomic_load_explicit(&ctab[i], memory_order_acquire);

		if (!e) {
			if (!mine) {
				mine = arena(sizeof(*mine) + len + 1);
				memset(mine, 0, sizeof(*mine));
				mine->hash = h;
				mine->len = (uint32_t)len;
				memcpy(mine->path, path, len);
				mine->path[len] = 0;
				mine->f.path = mine->path;
				atomic_init(&mine->state, st0);
			}
			if (atomic_compare_exchange_strong_explicit(&ctab[i], &e, mine,
					memory_order_acq_rel, memory_order_acquire)) {
				*ins = true;
				return mine;
			}
			/* mine is wasted arena space: rare */
		}
		if (e->hash == h && e->len == len && !memcmp(e->path, path, len))
			return e;
	}
}

static bool claim(centry *e, int from, int to)
{
	return atomic_compare_exchange_strong_explicit(&e->state, &from, to,
			memory_order_acq_rel, memory_order_acquire);
}

static void publish(centry *e, bool ok)
{
	atomic_store_explicit(&e->state, ok ? CE_OK : CE_MISSING, memory_order_release);
}

const vlex_file *vlex_get(const char *path, size_t len)
{
	bool ins;
	centry *e = centry_get(path, len, CE_LOADING, &ins);

	if (ins) {
		bool ok = load(&e->f, e->path);

		publish(e, ok);
		return ok ? &e->f : NULL;
	}
	for (int spin = 0;; spin++) {
		int st = atomic_load_explicit(&e->state, memory_order_acquire);

		if (st == CE_OK || st == CE_MISSING)
			return st == CE_OK ? &e->f : NULL;
		if (st == CE_QUEUED && claim(e, CE_QUEUED, CE_LOADING)) {
			/* not reached by the I/O threads yet: do it here */
			bool ok = load(&e->f, e->path);

			publish(e, ok);
			return ok ? &e->f : NULL;
		}
		if (st == CE_READ && claim(e, CE_READ, CE_LOADING)) {
			lex_src(&e->f, e->f.src, e->f.size);
			publish(e, true);
			return &e->f;
		}
		if (spin < 64)
			__asm__ volatile("yield");
		else
			sched_yield();
	}
}

/*
 * Read-ahead.  open() is effectively serialized in the macOS kernel
 * (~6 us per file however many threads call it, and each extra thread only
 * adds contention), so a few I/O threads open and read files that will be
 * needed, and the lexing threads only lex.
 */
enum { IO_THREADS = 3 };
static pthread_mutex_t ioq_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ioq_cv = PTHREAD_COND_INITIALIZER;
static centry **ioq;
static size_t ioq_head, ioq_tail, ioq_cap;

static void *io_thread(void *arg)
{
	(void)arg;
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
	for (;;) {
		centry *e;

		pthread_mutex_lock(&ioq_mu);
		while (ioq_head == ioq_tail)
			pthread_cond_wait(&ioq_cv, &ioq_mu);
		e = ioq[ioq_head++ % ioq_cap];
		pthread_mutex_unlock(&ioq_mu);
		if (claim(e, CE_QUEUED, CE_READING)) {
			const char *src;
			size_t size;

			if (read_src(e->path, &src, &size)) {
				e->f.src = src;
				e->f.size = (uint32_t)size;
				atomic_store_explicit(&e->state, CE_READ, memory_order_release);
			} else {
				publish(e, false);
			}
		}
	}
	return NULL;
}

static void start_io(void)
{
	const char *e = getenv("VLEX_IO_THREADS");
	int n = e ? atoi(e) : IO_THREADS;

	for (int i = 0; i < (n > 0 && n <= 32 ? n : IO_THREADS); i++) {
		pthread_t t;

		pthread_create(&t, NULL, io_thread, NULL);
		pthread_detach(t);
	}
}

void vlex_prefetch(const char *path, size_t len)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	bool ins;
	centry *e = centry_get(path, len, CE_QUEUED, &ins);

	if (!ins)
		return;
	pthread_mutex_lock(&ioq_mu);
	if (ioq_tail - ioq_head == ioq_cap) {	/* grow, keeping FIFO order */
		size_t cap = ioq_cap ? 2 * ioq_cap : 4096;
		centry **q = malloc(cap * sizeof(*q));

		for (size_t i = 0; i < ioq_tail - ioq_head; i++)
			q[i] = ioq[(ioq_head + i) % ioq_cap];
		ioq_tail -= ioq_head;
		ioq_head = 0;
		free(ioq);
		ioq = q;
		ioq_cap = cap;
	}
	ioq[ioq_tail++ % ioq_cap] = e;
	pthread_cond_signal(&ioq_cv);
	pthread_mutex_unlock(&ioq_mu);
	pthread_once(&once, start_io);
}
