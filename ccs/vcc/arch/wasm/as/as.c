/*
 * The WebAssembly assembler: LLVM's wasm `.s` text to the model of was.h,
 * which platform/wasm/wasm.c writes as an object.  Instructions are encoded
 * as they are read; operands that name symbols leave fixed-width LEB fields
 * and relocations, resolved when the object is laid out (as LLVM MC does:
 * a relocatable field is always 5 or 10 bytes).
 */
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "was.h"

/* ---------------- errors, buffers ---------------- */

int was_error(wmod *m, const char *fmt, ...)
{
	va_list ap;
	int k = 0;

	if (m->err[0])
		return -1;
	if (m->line)
		k = snprintf(m->err, m->errcap, "line %u: ", m->line);
	if (k < 0 || (size_t)k >= m->errcap)
		k = 0;
	va_start(ap, fmt);
	vsnprintf(m->err + k, m->errcap - k, fmt, ap);
	va_end(ap);
	return -1;
}

#define GROW(p, n, c) \
	do { \
		if ((n) >= (c)) { \
			(c) = (c) ? (c) * 2 : 16; \
			(p) = realloc((p), (size_t)(c) * sizeof(*(p))); \
			if (!(p)) \
				abort(); \
		} \
	} while (0)

static void put8(vbuf *b, uint8_t v) { vbuf_put(b, &v, 1); }

static void uleb(vbuf *b, uint64_t v)
{
	do {
		uint8_t c = v & 0x7f;

		v >>= 7;
		put8(b, c | (v ? 0x80 : 0));
	} while (v);
}

static void sleb(vbuf *b, int64_t v)
{
	for (;;) {
		uint8_t c = v & 0x7f;

		v >>= 7;
		if ((v == 0 && !(c & 0x40)) || (v == -1 && (c & 0x40))) {
			put8(b, c);
			return;
		}
		put8(b, c | 0x80);
	}
}

/* n zero bytes of a padded LEB, filled in when laid out */
static void hole(vbuf *b, int n)
{
	static const uint8_t z[10];

	vbuf_put(b, z, n);
}

/* ---------------- symbols, signatures ---------------- */

static uint32_t hname(const char *s, size_t n)
{
	uint32_t h = 2166136261u;

	for (size_t i = 0; i < n; i++)
		h = (h ^ (uint8_t)s[i]) * 16777619u;
	return h;
}

static void rehash(wmod *m)
{
	uint32_t cap = m->htcap ? m->htcap * 2 : 1024;

	free(m->ht);
	m->ht = calloc(cap, sizeof(uint32_t));
	if (!m->ht)
		abort();
	m->htcap = cap;
	for (uint32_t i = 0; i < m->nsym; i++) {
		uint32_t j = hname(m->sym[i].name, m->sym[i].nlen) & (cap - 1);

		while (m->ht[j])
			j = (j + 1) & (cap - 1);
		m->ht[j] = i + 1;
	}
}

uint32_t was_sym(wmod *m, const char *s, size_t n)
{
	uint32_t j;
	wsym *y;

	if (2 * (m->nsym + 1) > m->htcap)
		rehash(m);
	for (j = hname(s, n) & (m->htcap - 1); m->ht[j]; j = (j + 1) & (m->htcap - 1)) {
		y = &m->sym[m->ht[j] - 1];
		if (y->nlen == n && !memcmp(y->name, s, n))
			return m->ht[j] - 1;
	}
	GROW(m->sym, m->nsym, m->csym);
	y = &m->sym[m->nsym];
	memset(y, 0, sizeof *y);
	y->name = malloc(n + 1);
	memcpy(y->name, s, n);
	y->name[n] = 0;
	y->nlen = (uint32_t)n;
	y->kind = WS_NONE;
	y->sig = y->alias = y->seg = -1;
	y->temp = n >= 2 && s[0] == '.' && s[1] == 'L';
	m->ht[j] = ++m->nsym;
	return m->nsym - 1;
}

/* the order LLVM's MCAssembler sees symbols in: definitions, attribute
 * directives and uses in instructions and data */
void was_reg(wmod *m, uint32_t s)
{
	if (m->sym[s].reg)
		return;
	m->sym[s].reg = true;
	GROW(m->reg, m->nreg, m->creg);
	m->reg[m->nreg++] = s;
}

uint32_t was_sig_intern(wmod *m, const wsig *s)
{
	for (uint32_t i = 0; i < m->nsig; i++)
		if (m->sig[i].np == s->np && m->sig[i].nr == s->nr && !memcmp(m->sig[i].t, s->t, s->np + s->nr))
			return i;
	GROW(m->sig, m->nsig, m->csig);
	m->sig[m->nsig] = *s;
	return m->nsig++;
}

/* ---------------- the lexer ---------------- */

typedef struct lx {
	const char *p, *e;
} lx;

static void ws(lx *l)
{
	while (l->p < l->e && (*l->p == ' ' || *l->p == '\t'))
		l->p++;
}

static bool idch(char c)
{
	return isalnum((unsigned char)c) || c == '_' || c == '.' || c == '$' || c == '@';
}

/* an identifier (a symbol, mnemonic or type) */
static size_t ident(lx *l, const char **s)
{
	ws(l);
	*s = l->p;
	if (l->p < l->e && *l->p == '"') {
		/* a quoted name */
		const char *q = ++l->p;

		while (l->p < l->e && *l->p != '"')
			l->p++;
		*s = q;
		if (l->p < l->e)
			l->p++;
		return (size_t)(l->p - q - 1);
	}
	while (l->p < l->e && idch(*l->p))
		l->p++;
	return (size_t)(l->p - *s);
}

static bool accept(lx *l, char c)
{
	ws(l);
	if (l->p < l->e && *l->p == c) {
		l->p++;
		return true;
	}
	return false;
}

static bool at_end(lx *l)
{
	ws(l);
	return l->p >= l->e || *l->p == '#';
}

static bool number(lx *l, int64_t *v)
{
	const char *s;
	char buf[80];
	char *end;
	size_t n;
	bool neg = false;

	ws(l);
	s = l->p;
	if (s < l->e && (*s == '-' || *s == '+')) {
		neg = *s == '-';
		s++;
	}
	if (s >= l->e || !isdigit((unsigned char)*s))
		return false;
	n = 0;
	while (s + n < l->e && isalnum((unsigned char)s[n]) && n < sizeof buf - 1)
		n++;
	memcpy(buf, s, n);
	buf[n] = 0;
	errno = 0;
	*v = (int64_t)strtoull(buf, &end, 0);
	if (*end)
		return false;
	if (neg)
		*v = (int64_t)(0 - (uint64_t)*v);
	l->p = s + n;
	return true;
}

/* expr: number | sym [(+|-) number] | number + sym */
static bool expr(wmod *m, lx *l, int64_t *v, int32_t *sym)
{
	const char *s;
	size_t n;
	int64_t k;

	*v = 0;
	*sym = -1;
	if (number(l, v)) {
		if (accept(l, '+')) {
			n = ident(l, &s);
			if (!n)
				return false;
			*sym = (int32_t)was_sym(m, s, n);
		}
		return true;
	}
	n = ident(l, &s);
	if (!n)
		return false;
	*sym = (int32_t)was_sym(m, s, n);
	ws(l);
	if (l->p < l->e && (*l->p == '+' || *l->p == '-')) {
		bool neg = *l->p == '-';

		l->p++;
		if (!number(l, &k))
			return false;
		*v = neg ? -k : k;
	}
	return true;
}

static int valtype(const char *s, size_t n)
{
	if (n == 3 && !memcmp(s, "i32", 3)) return WV_I32;
	if (n == 3 && !memcmp(s, "i64", 3)) return WV_I64;
	if (n == 3 && !memcmp(s, "f32", 3)) return WV_F32;
	if (n == 3 && !memcmp(s, "f64", 3)) return WV_F64;
	if (n == 7 && !memcmp(s, "funcref", 7)) return WV_FUNCREF;
	return -1;
}

/* (t, ...) -> (t, ...) */
static int sigparse(wmod *m, lx *l, wsig *g)
{
	memset(g, 0, sizeof *g);
	for (int part = 0; part < 2; part++) {
		if (part && !(accept(l, '-') && accept(l, '>')))
			return was_error(m, "expected -> in a signature");
		if (!accept(l, '('))
			return was_error(m, "expected ( in a signature");
		if (!accept(l, ')')) {
			do {
				const char *s;
				size_t n = ident(l, &s);
				int t = valtype(s, n);

				if (t < 0 || t == WV_FUNCREF)
					return was_error(m, "unknown value type '%.*s'", (int)n, s);
				if (g->np + g->nr >= sizeof g->t)
					return was_error(m, "signature too long");
				g->t[g->np + g->nr] = (uint8_t)t;
				if (part)
					g->nr++;
				else
					g->np++;
			} while (accept(l, ','));
			if (!accept(l, ')'))
				return was_error(m, "expected ) in a signature");
		}
	}
	return 0;
}

/* ---------------- instructions ---------------- */

enum {
	I_NONE, I_BLOCK, I_LABEL, I_BRTABLE, I_CALL, I_CALLIND, I_LOCAL, I_GLOBAL, I_I32, I_I64, I_F32, I_F64,
	I_MEM, I_MEMIDX, I_MEMIDX2, I_END, I_ENDFN, I_TAG,
};

typedef struct winsn {
	const char *name;
	uint16_t op;		/* 0xfcNN: the 0xfc prefix */
	uint8_t imm;
	uint8_t align;		/* memory: natural alignment (log2) */
} winsn;

static const winsn insns[] = {
	{ "unreachable", 0x00, I_NONE, 0 }, { "nop", 0x01, I_NONE, 0 }, { "block", 0x02, I_BLOCK, 0 },
	{ "loop", 0x03, I_BLOCK, 0 }, { "if", 0x04, I_BLOCK, 0 }, { "else", 0x05, I_NONE, 0 },
	{ "end_block", 0x0b, I_END, 0 }, { "end_loop", 0x0b, I_END, 0 }, { "end_if", 0x0b, I_END, 0 },
	{ "end", 0x0b, I_END, 0 }, { "end_function", 0x0b, I_ENDFN, 0 },
	{ "try", 0x06, I_BLOCK, 0 }, { "catch", 0x07, I_TAG, 0 }, { "throw", 0x08, I_TAG, 0 },
	{ "rethrow", 0x09, I_LABEL, 0 }, { "end_try", 0x0b, I_END, 0 }, { "delegate", 0x18, I_LABEL, 0 },
	{ "catch_all", 0x19, I_NONE, 0 },
	{ "br", 0x0c, I_LABEL, 0 }, { "br_if", 0x0d, I_LABEL, 0 }, { "br_table", 0x0e, I_BRTABLE, 0 },
	{ "return", 0x0f, I_NONE, 0 }, { "call", 0x10, I_CALL, 0 }, { "call_indirect", 0x11, I_CALLIND, 0 },
	{ "drop", 0x1a, I_NONE, 0 }, { "select", 0x1b, I_NONE, 0 }, { "i32.select", 0x1b, I_NONE, 0 },
	{ "i64.select", 0x1b, I_NONE, 0 }, { "f32.select", 0x1b, I_NONE, 0 }, { "f64.select", 0x1b, I_NONE, 0 },
	{ "local.get", 0x20, I_LOCAL, 0 }, { "local.set", 0x21, I_LOCAL, 0 }, { "local.tee", 0x22, I_LOCAL, 0 },
	{ "global.get", 0x23, I_GLOBAL, 0 }, { "global.set", 0x24, I_GLOBAL, 0 },
	{ "i32.load", 0x28, I_MEM, 2 }, { "i64.load", 0x29, I_MEM, 3 }, { "f32.load", 0x2a, I_MEM, 2 },
	{ "f64.load", 0x2b, I_MEM, 3 }, { "i32.load8_s", 0x2c, I_MEM, 0 }, { "i32.load8_u", 0x2d, I_MEM, 0 },
	{ "i32.load16_s", 0x2e, I_MEM, 1 }, { "i32.load16_u", 0x2f, I_MEM, 1 }, { "i64.load8_s", 0x30, I_MEM, 0 },
	{ "i64.load8_u", 0x31, I_MEM, 0 }, { "i64.load16_s", 0x32, I_MEM, 1 }, { "i64.load16_u", 0x33, I_MEM, 1 },
	{ "i64.load32_s", 0x34, I_MEM, 2 }, { "i64.load32_u", 0x35, I_MEM, 2 }, { "i32.store", 0x36, I_MEM, 2 },
	{ "i64.store", 0x37, I_MEM, 3 }, { "f32.store", 0x38, I_MEM, 2 }, { "f64.store", 0x39, I_MEM, 3 },
	{ "i32.store8", 0x3a, I_MEM, 0 }, { "i32.store16", 0x3b, I_MEM, 1 }, { "i64.store8", 0x3c, I_MEM, 0 },
	{ "i64.store16", 0x3d, I_MEM, 1 }, { "i64.store32", 0x3e, I_MEM, 2 },
	{ "memory.size", 0x3f, I_MEMIDX, 0 }, { "memory.grow", 0x40, I_MEMIDX, 0 },
	{ "i32.const", 0x41, I_I32, 0 }, { "i64.const", 0x42, I_I64, 0 }, { "f32.const", 0x43, I_F32, 0 },
	{ "f64.const", 0x44, I_F64, 0 },
	{ "i32.eqz", 0x45, I_NONE, 0 }, { "i32.eq", 0x46, I_NONE, 0 }, { "i32.ne", 0x47, I_NONE, 0 }, { "i32.lt_s", 0x48, I_NONE, 0 }, { "i32.lt_u", 0x49, I_NONE, 0 },
	{ "i32.gt_s", 0x4a, I_NONE, 0 }, { "i32.gt_u", 0x4b, I_NONE, 0 }, { "i32.le_s", 0x4c, I_NONE, 0 }, { "i32.le_u", 0x4d, I_NONE, 0 }, { "i32.ge_s", 0x4e, I_NONE, 0 },
	{ "i32.ge_u", 0x4f, I_NONE, 0 },
	{ "i64.eqz", 0x50, I_NONE, 0 }, { "i64.eq", 0x51, I_NONE, 0 }, { "i64.ne", 0x52, I_NONE, 0 }, { "i64.lt_s", 0x53, I_NONE, 0 }, { "i64.lt_u", 0x54, I_NONE, 0 },
	{ "i64.gt_s", 0x55, I_NONE, 0 }, { "i64.gt_u", 0x56, I_NONE, 0 }, { "i64.le_s", 0x57, I_NONE, 0 }, { "i64.le_u", 0x58, I_NONE, 0 }, { "i64.ge_s", 0x59, I_NONE, 0 },
	{ "i64.ge_u", 0x5a, I_NONE, 0 },
	{ "f32.eq", 0x5b, I_NONE, 0 }, { "f32.ne", 0x5c, I_NONE, 0 }, { "f32.lt", 0x5d, I_NONE, 0 }, { "f32.gt", 0x5e, I_NONE, 0 }, { "f32.le", 0x5f, I_NONE, 0 },
	{ "f32.ge", 0x60, I_NONE, 0 }, { "f64.eq", 0x61, I_NONE, 0 }, { "f64.ne", 0x62, I_NONE, 0 }, { "f64.lt", 0x63, I_NONE, 0 }, { "f64.gt", 0x64, I_NONE, 0 },
	{ "f64.le", 0x65, I_NONE, 0 }, { "f64.ge", 0x66, I_NONE, 0 },
	{ "i32.clz", 0x67, I_NONE, 0 }, { "i32.ctz", 0x68, I_NONE, 0 }, { "i32.popcnt", 0x69, I_NONE, 0 }, { "i32.add", 0x6a, I_NONE, 0 }, { "i32.sub", 0x6b, I_NONE, 0 },
	{ "i32.mul", 0x6c, I_NONE, 0 }, { "i32.div_s", 0x6d, I_NONE, 0 }, { "i32.div_u", 0x6e, I_NONE, 0 }, { "i32.rem_s", 0x6f, I_NONE, 0 }, { "i32.rem_u", 0x70, I_NONE, 0 },
	{ "i32.and", 0x71, I_NONE, 0 }, { "i32.or", 0x72, I_NONE, 0 }, { "i32.xor", 0x73, I_NONE, 0 }, { "i32.shl", 0x74, I_NONE, 0 }, { "i32.shr_s", 0x75, I_NONE, 0 },
	{ "i32.shr_u", 0x76, I_NONE, 0 }, { "i32.rotl", 0x77, I_NONE, 0 }, { "i32.rotr", 0x78, I_NONE, 0 },
	{ "i64.clz", 0x79, I_NONE, 0 }, { "i64.ctz", 0x7a, I_NONE, 0 }, { "i64.popcnt", 0x7b, I_NONE, 0 }, { "i64.add", 0x7c, I_NONE, 0 }, { "i64.sub", 0x7d, I_NONE, 0 },
	{ "i64.mul", 0x7e, I_NONE, 0 }, { "i64.div_s", 0x7f, I_NONE, 0 }, { "i64.div_u", 0x80, I_NONE, 0 }, { "i64.rem_s", 0x81, I_NONE, 0 }, { "i64.rem_u", 0x82, I_NONE, 0 },
	{ "i64.and", 0x83, I_NONE, 0 }, { "i64.or", 0x84, I_NONE, 0 }, { "i64.xor", 0x85, I_NONE, 0 }, { "i64.shl", 0x86, I_NONE, 0 }, { "i64.shr_s", 0x87, I_NONE, 0 },
	{ "i64.shr_u", 0x88, I_NONE, 0 }, { "i64.rotl", 0x89, I_NONE, 0 }, { "i64.rotr", 0x8a, I_NONE, 0 },
	{ "f32.abs", 0x8b, I_NONE, 0 }, { "f32.neg", 0x8c, I_NONE, 0 }, { "f32.ceil", 0x8d, I_NONE, 0 }, { "f32.floor", 0x8e, I_NONE, 0 }, { "f32.trunc", 0x8f, I_NONE, 0 },
	{ "f32.nearest", 0x90, I_NONE, 0 }, { "f32.sqrt", 0x91, I_NONE, 0 }, { "f32.add", 0x92, I_NONE, 0 }, { "f32.sub", 0x93, I_NONE, 0 }, { "f32.mul", 0x94, I_NONE, 0 },
	{ "f32.div", 0x95, I_NONE, 0 }, { "f32.min", 0x96, I_NONE, 0 }, { "f32.max", 0x97, I_NONE, 0 }, { "f32.copysign", 0x98, I_NONE, 0 },
	{ "f64.abs", 0x99, I_NONE, 0 }, { "f64.neg", 0x9a, I_NONE, 0 }, { "f64.ceil", 0x9b, I_NONE, 0 }, { "f64.floor", 0x9c, I_NONE, 0 }, { "f64.trunc", 0x9d, I_NONE, 0 },
	{ "f64.nearest", 0x9e, I_NONE, 0 }, { "f64.sqrt", 0x9f, I_NONE, 0 }, { "f64.add", 0xa0, I_NONE, 0 }, { "f64.sub", 0xa1, I_NONE, 0 }, { "f64.mul", 0xa2, I_NONE, 0 },
	{ "f64.div", 0xa3, I_NONE, 0 }, { "f64.min", 0xa4, I_NONE, 0 }, { "f64.max", 0xa5, I_NONE, 0 }, { "f64.copysign", 0xa6, I_NONE, 0 },
	{ "i32.wrap_i64", 0xa7, I_NONE, 0 }, { "i32.trunc_f32_s", 0xa8, I_NONE, 0 }, { "i32.trunc_f32_u", 0xa9, I_NONE, 0 }, { "i32.trunc_f64_s", 0xaa, I_NONE, 0 },
	{ "i32.trunc_f64_u", 0xab, I_NONE, 0 }, { "i64.extend_i32_s", 0xac, I_NONE, 0 }, { "i64.extend_i32_u", 0xad, I_NONE, 0 },
	{ "i64.trunc_f32_s", 0xae, I_NONE, 0 }, { "i64.trunc_f32_u", 0xaf, I_NONE, 0 }, { "i64.trunc_f64_s", 0xb0, I_NONE, 0 }, { "i64.trunc_f64_u", 0xb1, I_NONE, 0 },
	{ "f32.convert_i32_s", 0xb2, I_NONE, 0 }, { "f32.convert_i32_u", 0xb3, I_NONE, 0 }, { "f32.convert_i64_s", 0xb4, I_NONE, 0 },
	{ "f32.convert_i64_u", 0xb5, I_NONE, 0 }, { "f32.demote_f64", 0xb6, I_NONE, 0 }, { "f64.convert_i32_s", 0xb7, I_NONE, 0 },
	{ "f64.convert_i32_u", 0xb8, I_NONE, 0 }, { "f64.convert_i64_s", 0xb9, I_NONE, 0 }, { "f64.convert_i64_u", 0xba, I_NONE, 0 },
	{ "f64.promote_f32", 0xbb, I_NONE, 0 }, { "i32.reinterpret_f32", 0xbc, I_NONE, 0 }, { "i64.reinterpret_f64", 0xbd, I_NONE, 0 },
	{ "f32.reinterpret_i32", 0xbe, I_NONE, 0 }, { "f64.reinterpret_i64", 0xbf, I_NONE, 0 },
	{ "i32.extend8_s", 0xc0, I_NONE, 0 }, { "i32.extend16_s", 0xc1, I_NONE, 0 }, { "i64.extend8_s", 0xc2, I_NONE, 0 }, { "i64.extend16_s", 0xc3, I_NONE, 0 },
	{ "i64.extend32_s", 0xc4, I_NONE, 0 },
	{ "i32.trunc_sat_f32_s", 0xfc00, I_NONE, 0 }, { "i32.trunc_sat_f32_u", 0xfc01, I_NONE, 0 }, { "i32.trunc_sat_f64_s", 0xfc02, I_NONE, 0 },
	{ "i32.trunc_sat_f64_u", 0xfc03, I_NONE, 0 }, { "i64.trunc_sat_f32_s", 0xfc04, I_NONE, 0 }, { "i64.trunc_sat_f32_u", 0xfc05, I_NONE, 0 },
	{ "i64.trunc_sat_f64_s", 0xfc06, I_NONE, 0 }, { "i64.trunc_sat_f64_u", 0xfc07, I_NONE, 0 },
	{ "memory.copy", 0xfc0a, I_MEMIDX2, 0 }, { "memory.fill", 0xfc0b, I_MEMIDX, 0 },
};

static const winsn *insn_find(const char *s, size_t n)
{
	static uint16_t *ht;	/* name hash -> insn + 1 (built once; races write the same values) */
	static uint32_t cap = 1024;
	static int built;
	uint32_t j;

	if (!__atomic_load_n(&built, __ATOMIC_ACQUIRE)) {
		uint16_t *t = calloc(cap, sizeof *t);

		if (!t)
			abort();
		for (uint32_t i = 0; i < sizeof insns / sizeof insns[0]; i++) {
			const char *nm = insns[i].name;

			for (j = hname(nm, strlen(nm)) & (cap - 1); t[j]; j = (j + 1) & (cap - 1))
				;
			t[j] = (uint16_t)(i + 1);
		}
		ht = t;
		__atomic_store_n(&built, 1, __ATOMIC_RELEASE);
	}
	for (j = hname(s, n) & (cap - 1); ht[j]; j = (j + 1) & (cap - 1)) {
		const winsn *w = &insns[ht[j] - 1];

		if (strlen(w->name) == n && !memcmp(w->name, s, n))
			return w;
	}
	return NULL;
}

/* ---------------- the assembler state ---------------- */

typedef struct was {
	wmod *m;
	int cur;		/* 0 none, 1 a text section, 2 a data segment, 3 .init_array */
	uint32_t prio;		/* .init_array's priority */
	bool custom;		/* the data segment is a custom section (m->cust) */
	int32_t seg;		/* the data segment */
	int32_t fn;		/* the function being assembled, -1 */
	int32_t label;		/* the last label in the text section */
} was;

static void rel_add(wrel **r, uint32_t *n, uint32_t *c, uint8_t type, uint32_t off, uint32_t sym, int64_t addend)
{
	GROW(*r, *n, *c);
	(*r)[(*n)++] = (wrel){ type, off, sym, addend };
}

static void fn_rel(was *a, uint8_t type, uint32_t sym, int64_t addend)
{
	wfunc *f = &a->m->fn[a->fn];

	rel_add(&f->rel, &f->nrel, &f->crel, type, (uint32_t)f->body.n, sym, addend);
}

static uint32_t sym_use(was *a, uint32_t s, int kind)
{
	wsym *y = &a->m->sym[s];

	was_reg(a->m, s);
	y->used_reloc = true;
	if (kind != WS_NONE && y->kind == WS_NONE)
		y->kind = (uint8_t)kind;
	return s;
}

static int instruction(was *a, const winsn *w, lx *l)
{
	wmod *m = a->m;
	wfunc *f = &m->fn[a->fn];
	vbuf *b = &f->body;
	int64_t v;
	int32_t s;
	const char *t;
	size_t n;

	if (w->op >= 0xfc00) {
		put8(b, 0xfc);
		uleb(b, w->op & 0xff);
	} else {
		put8(b, (uint8_t)w->op);
	}
	switch (w->imm) {
	case I_NONE:
	case I_END:
		break;
	case I_ENDFN:
		f->done = true;
		a->fn = -1;
		break;
	case I_BLOCK: {
		int vt = -1;

		n = ident(l, &t);
		if (n) {
			vt = valtype(t, n);
			if (vt < 0 || vt == WV_FUNCREF)
				return was_error(m, "unknown block type '%.*s'", (int)n, t);
		}
		put8(b, vt < 0 ? 0x40 : (uint8_t)vt);
		break;
	}
	case I_LABEL:
	case I_LOCAL:
		if (!number(l, &v) || v < 0)
			return was_error(m, "%s: expected a number", w->name);
		uleb(b, (uint64_t)v);
		break;
	case I_BRTABLE: {
		/* {a, b, ..., default} */
		int64_t d[1024];
		vbuf tmp = { 0 };
		uint32_t k = 0;

		if (!accept(l, '{'))
			return was_error(m, "br_table: expected {");
		do {
			if (!number(l, &v) || v < 0)
				return was_error(m, "br_table: expected a depth");
			if (k < 1024)
				d[k] = v;
			else
				uleb(&tmp, (uint64_t)v);
			k++;
		} while (accept(l, ','));
		if (!accept(l, '}'))
			return was_error(m, "br_table: expected }");
		uleb(b, k - 1);
		for (uint32_t i = 0; i < k && i < 1024; i++)
			uleb(b, (uint64_t)d[i]);
		if (k > 1024) {
			vbuf_put(b, tmp.p, tmp.n);
			free(tmp.p);
		}
		break;
	}
	case I_CALL:
		n = ident(l, &t);
		if (!n)
			return was_error(m, "call: expected a function");
		s = (int32_t)sym_use(a, was_sym(m, t, n), WS_FUNCTION);
		fn_rel(a, R_FUNCTION_INDEX_LEB, (uint32_t)s, 0);
		hole(b, 5);
		break;
	case I_CALLIND: {
		wsig g;
		uint32_t tab;

		ws(l);
		if (l->p < l->e && *l->p != '(') {
			n = ident(l, &t);
			if (!n || !accept(l, ','))
				return was_error(m, "call_indirect: expected a table and a signature");
		} else {
			t = "__indirect_function_table";
			n = strlen(t);
		}
		tab = was_sym(m, t, n);
		if (sigparse(m, l, &g))
			return -1;
		{
			/* LLVM registers the signature (a temporary symbol) here, among the symbols */
			uint32_t si = was_sig_intern(m, &g);

			if (m->nci >= m->cci) {
				m->cci = m->cci ? 2 * m->cci : 16;
				m->ci_at = realloc(m->ci_at, m->cci * sizeof(uint32_t));
				m->ci_sig = realloc(m->ci_sig, m->cci * sizeof(uint32_t));
				if (!m->ci_at || !m->ci_sig)
					abort();
			}
			m->ci_at[m->nci] = m->nreg;
			m->ci_sig[m->nci++] = si;
			fn_rel(a, R_TYPE_INDEX_LEB, si, 0);
		}
		hole(b, 5);
		sym_use(a, tab, WS_TABLE);
		fn_rel(a, R_TABLE_NUMBER_LEB, tab, 0);
		hole(b, 5);
		break;
	}
	case I_TAG:
		n = ident(l, &t);
		if (!n)
			return was_error(m, "%s: expected a tag", w->name);
		s = (int32_t)sym_use(a, was_sym(m, t, n), WS_TAG);
		fn_rel(a, R_TAG_INDEX_LEB, (uint32_t)s, 0);
		hole(b, 5);
		break;
	case I_GLOBAL:
		n = ident(l, &t);
		if (!n)
			return was_error(m, "%s: expected a global", w->name);
		s = (int32_t)sym_use(a, was_sym(m, t, n), WS_GLOBAL);
		fn_rel(a, R_GLOBAL_INDEX_LEB, (uint32_t)s, 0);
		hole(b, 5);
		break;
	case I_I32:
	case I_I64:
		if (!expr(m, l, &v, &s))
			return was_error(m, "%s: expected a number or a symbol", w->name);
		if (s < 0) {
			sleb(b, w->imm == I_I32 ? (int64_t)(int32_t)v : v);
			break;
		}
		sym_use(a, (uint32_t)s, WS_NONE);
		fn_rel(a, w->imm == I_I32 ? R_ADDR_SLEB : R_ADDR_SLEB64, (uint32_t)s, v);
		hole(b, w->imm == I_I32 ? 5 : 10);
		break;
	case I_F32:
	case I_F64: {
		const char *p;
		char buf[128];
		size_t k = 0;
		double d;

		ws(l);
		p = l->p;
		while (l->p < l->e && *l->p != ' ' && *l->p != '\t' && *l->p != '#' && k < sizeof buf - 1)
			buf[k++] = *l->p++;
		buf[k] = 0;
		d = strtod(buf, NULL);
		(void)p;
		if (w->imm == I_F32) {
			float x = (float)d;

			vbuf_put(b, &x, 4);
		} else {
			vbuf_put(b, &d, 8);
		}
		break;
	}
	case I_MEM: {
		int64_t off = 0, al = w->align;
		int32_t os = -1;

		if (!at_end(l)) {
			if (!expr(m, l, &off, &os))
				return was_error(m, "%s: expected an offset", w->name);
			if (accept(l, ':')) {
				n = ident(l, &t);
				if (n != 7 || memcmp(t, "p2align", 7) || !accept(l, '=') || !number(l, &al))
					return was_error(m, "%s: expected :p2align=N", w->name);
			}
		}
		uleb(b, (uint64_t)al);
		if (os >= 0) {
			/* a symbol's address as the offset (clang -O0) */
			sym_use(a, (uint32_t)os, WS_NONE);
			fn_rel(a, R_MEMORY_ADDR_LEB64, (uint32_t)os, off);
			hole(b, 10);
		} else {
			uleb(b, (uint64_t)off);
		}
		break;
	}
	case I_MEMIDX:
	case I_MEMIDX2:
		for (int i = 0; i < (w->imm == I_MEMIDX2 ? 2 : 1); i++) {
			v = 0;
			if (i)
				accept(l, ',');
			if (!at_end(l) && !number(l, &v))
				return was_error(m, "%s: expected a memory index", w->name);
			uleb(b, (uint64_t)v);
		}
		break;
	}
	if (!at_end(l))
		return was_error(m, "%s: unexpected '%.*s'", w->name, (int)(l->e - l->p), l->p);
	return 0;
}

/* ---------------- data ---------------- */

static wseg *curseg(was *a) { return a->custom ? &a->m->cust[a->seg] : &a->m->seg[a->seg]; }

static int data_value(was *a, lx *l, int size)
{
	wmod *m = a->m;
	wseg *g = curseg(a);

	do {
		int64_t v;
		int32_t s;

		if (!expr(m, l, &v, &s))
			return was_error(m, "expected a value");
		if (s >= 0) {
			if (a->custom)
				return was_error(m, "a symbol in a custom section");
			if (size != 8 && size != 4)
				return was_error(m, "a symbol in a %d-byte value", size);
			sym_use(a, (uint32_t)s, WS_NONE);
			rel_add(&g->rel, &g->nrel, &g->crel, size == 8 ? R_ADDR_I64 : R_ADDR_I32, (uint32_t)g->data.n,
				(uint32_t)s, v);
			v = 0;
		}
		vbuf_put(&g->data, &v, (size_t)size);	/* (little-endian host) */
	} while (accept(l, ','));
	return 0;
}

static int data_string(was *a, lx *l, bool nul)
{
	wmod *m = a->m;
	vbuf *b = &curseg(a)->data;

	do {
		if (!accept(l, '"'))
			return was_error(m, "expected a string");
		while (l->p < l->e && *l->p != '"') {
			int c = (unsigned char)*l->p++;

			if (c == '\\' && l->p < l->e) {
				c = (unsigned char)*l->p++;
				switch (c) {
				case 'n': c = '\n'; break;
				case 't': c = '\t'; break;
				case 'r': c = '\r'; break;
				case 'b': c = '\b'; break;
				case 'f': c = '\f'; break;
				case 'v': c = '\v'; break;
				case 'a': c = '\a'; break;
				case 'x': {
					int k = 0;

					c = 0;
					while (l->p < l->e && isxdigit((unsigned char)*l->p) && k++ < 2) {
						int d = *l->p++;

						c = c * 16 + (isdigit(d) ? d - '0' : (tolower(d) - 'a' + 10));
					}
					break;
				}
				default:
					if (c >= '0' && c <= '7') {
						int k = 1;

						c -= '0';
						while (l->p < l->e && *l->p >= '0' && *l->p <= '7' && k++ < 3)
							c = c * 8 + (*l->p++ - '0');
					}
					break;
				}
			}
			put8(b, (uint8_t)c);
		}
		if (!accept(l, '"'))
			return was_error(m, "unterminated string");
		if (nul)
			put8(b, 0);
	} while (accept(l, ','));
	return 0;
}

/* ---------------- directives ---------------- */

static int section(was *a, const char *s, size_t n)
{
	wmod *m = a->m;

	a->fn = -1;
	a->custom = false;
	if ((n >= 5 && !memcmp(s, ".text", 5) && (n == 5 || s[5] == '.'))) {
		a->cur = 1;
		a->label = -1;
		return 0;
	}
	if (n >= 11 && !memcmp(s, ".init_array", 11) && (n == 11 || s[11] == '.')) {
		/* not a segment: its pointers become the linking section's init functions */
		a->cur = 3;
		a->prio = 65535;
		if (n > 12) {
			a->prio = 0;
			for (size_t i = 12; i < n; i++) {
				if (s[i] < '0' || s[i] > '9' || a->prio > 6553500)
					return was_error(m, "bad .init_array priority");
				a->prio = a->prio * 10 + (uint32_t)(s[i] - '0');
			}
		}
		return 0;
	}
	if (n > 16 && !memcmp(s, ".custom_section.", 16)) {
		uint32_t i;

		s += 16, n -= 16;
		for (i = 0; i < m->ncust; i++)
			if (strlen(m->cust[i].name) == n && !memcmp(m->cust[i].name, s, n))
				break;
		if (i == m->ncust) {
			GROW(m->cust, m->ncust, m->ccust);
			memset(&m->cust[i], 0, sizeof(wseg));
			m->cust[i].name = malloc(n + 1);
			memcpy(m->cust[i].name, s, n);
			m->cust[i].name[n] = 0;
			m->ncust++;
		}
		a->cur = 2;
		a->custom = true;
		a->seg = (int32_t)i;
		return 0;
	}
	if (n >= 6 && (!memcmp(s, ".tdata", 6) || !memcmp(s, ".tbss", 5)))
		return was_error(m, "thread-local data is not supported yet");
	for (uint32_t i = 0; i < m->nseg; i++)
		if (strlen(m->seg[i].name) == n && !memcmp(m->seg[i].name, s, n)) {
			a->cur = 2;
			a->seg = (int32_t)i;
			return 0;
		}
	GROW(m->seg, m->nseg, m->cseg);
	memset(&m->seg[m->nseg], 0, sizeof(wseg));
	m->seg[m->nseg].name = malloc(n + 1);
	memcpy(m->seg[m->nseg].name, s, n);
	m->seg[m->nseg].name[n] = 0;
	a->cur = 2;
	a->seg = (int32_t)m->nseg++;
	return 0;
}

static int sym_list(was *a, lx *l, void (*f)(wsym *))
{
	do {
		const char *s;
		size_t n = ident(l, &s);
		uint32_t k;

		if (!n)
			return was_error(a->m, "expected a symbol");
		k = was_sym(a->m, s, n);
		was_reg(a->m, k);
		f(&a->m->sym[k]);
	} while (accept(l, ','));
	return 0;
}

static void set_global(wsym *y) { y->global = true; }
static void set_weak(wsym *y) { y->weak = true; }
static void set_hidden(wsym *y) { y->hidden = true; }

static int directive(was *a, const char *d, size_t dn, lx *l)
{
	wmod *m = a->m;
	const char *s;
	size_t n;
	int64_t v;
	int32_t sy;

#define IS(x) (dn == sizeof(x) - 1 && !memcmp(d, x, dn))
	if (IS(".section")) {
		ws(l);
		s = l->p;
		if (l->p < l->e && *l->p == '"') {
			n = ident(l, &s);	/* a quoted name */
		} else {
			while (l->p < l->e && *l->p != ',' && *l->p != ' ' && *l->p != '\t')
				l->p++;
			n = (size_t)(l->p - s);
		}
		if (section(a, s, n))
			return -1;
		/* flags: S, a segment of strings */
		if (accept(l, ',') && accept(l, '"'))
			while (l->p < l->e && *l->p != '"')
				if (*l->p++ == 'S' && a->cur == 2 && !a->custom)
					curseg(a)->flags |= 1;
		return 0;
	}
	if (IS(".text"))
		return section(a, ".text", 5);
	if (IS(".data"))
		return section(a, ".data", 5);
	if (IS(".bss"))
		return section(a, ".bss", 4);
	if (IS(".globl") || IS(".global"))
		return sym_list(a, l, set_global);
	if (IS(".weak"))
		return sym_list(a, l, set_weak);
	if (IS(".hidden"))
		return sym_list(a, l, set_hidden);
	if (IS(".type")) {
		n = ident(l, &s);
		if (!n || !accept(l, ','))
			return was_error(m, ".type: expected a symbol");
		sy = (int32_t)was_sym(m, s, n);
		was_reg(m, (uint32_t)sy);
		accept(l, '@');
		accept(l, '%');
		n = ident(l, &s);
		if (n == 8 && !memcmp(s, "function", 8))
			m->sym[sy].kind = WS_FUNCTION;
		else if (n == 6 && !memcmp(s, "object", 6) && m->sym[sy].kind == WS_NONE)
			m->sym[sy].kind = WS_DATA;
		return 0;
	}
	if (IS(".functype")) {
		wsig g;

		n = ident(l, &s);
		if (!n)
			return was_error(m, ".functype: expected a symbol");
		sy = (int32_t)was_sym(m, s, n);
		if (sigparse(m, l, &g))
			return -1;
		m->sym[sy].sig = (int32_t)was_sig_intern(m, &g);
		m->sym[sy].kind = WS_FUNCTION;
		if (a->cur == 1 && a->fn < 0 && a->label == sy) {
			/* the start of the function at the label */
			wfunc *f;

			if (m->sym[sy].seg >= 0)
				return was_error(m, "function '%s' defined twice", m->sym[sy].name);
			GROW(m->fn, m->nfn, m->cfn);
			f = &m->fn[m->nfn];
			memset(f, 0, sizeof *f);
			f->sym = (uint32_t)sy;
			f->sig = m->sym[sy].sig;
			m->sym[sy].seg = (int32_t)m->nfn;
			a->fn = (int32_t)m->nfn++;
		}
		return 0;
	}
	if (IS(".local")) {
		if (a->fn < 0)
			return was_error(m, ".local outside a function");
		if (at_end(l))
			return 0;
		do {
			int t;

			n = ident(l, &s);
			t = valtype(s, n);
			if (t < 0 || t == WV_FUNCREF)
				return was_error(m, ".local: unknown type '%.*s'", (int)n, s);
			put8(&m->fn[a->fn].locals, (uint8_t)t);
		} while (accept(l, ','));
		return 0;
	}
	if (IS(".globaltype")) {
		int t;

		n = ident(l, &s);
		if (!n || !accept(l, ','))
			return was_error(m, ".globaltype: expected a symbol and a type");
		sy = (int32_t)was_sym(m, s, n);
		n = ident(l, &s);
		t = valtype(s, n);
		if (t < 0 || t == WV_FUNCREF)
			return was_error(m, ".globaltype: unknown type");
		m->sym[sy].kind = WS_GLOBAL;
		m->sym[sy].gtype = (uint8_t)t;
		m->sym[sy].gmut = true;
		if (accept(l, ',')) {
			n = ident(l, &s);
			if (n == 9 && !memcmp(s, "immutable", 9))
				m->sym[sy].gmut = false;
		}
		return 0;
	}
	if (IS(".tagtype")) {
		wsig g = { 0 };

		n = ident(l, &s);
		if (!n)
			return was_error(m, ".tagtype: expected a symbol");
		sy = (int32_t)was_sym(m, s, n);
		while (!at_end(l)) {
			int t;

			n = ident(l, &s);
			t = valtype(s, n);
			if (t < 0 || t == WV_FUNCREF || g.np >= sizeof g.t)
				return was_error(m, ".tagtype: unknown type '%.*s'", (int)n, s);
			g.t[g.np++] = (uint8_t)t;
			accept(l, ',');
		}
		m->sym[sy].kind = WS_TAG;
		m->sym[sy].sig = (int32_t)was_sig_intern(m, &g);
		return 0;
	}
	if (IS(".tabletype")) {
		n = ident(l, &s);
		if (!n)
			return was_error(m, ".tabletype: expected a symbol");
		sy = (int32_t)was_sym(m, s, n);
		m->sym[sy].kind = WS_TABLE;
		l->p = l->e;
		return 0;
	}
	if (IS(".size")) {
		n = ident(l, &s);
		if (!n || !accept(l, ','))
			return was_error(m, ".size: expected a symbol");
		sy = (int32_t)was_sym(m, s, n);
		if (!number(l, &v))
			return was_error(m, ".size: expected a number");
		m->sym[sy].size = (uint64_t)v;
		return 0;
	}
	if (IS(".set")) {
		int32_t t;

		n = ident(l, &s);
		if (!n || !accept(l, ','))
			return was_error(m, ".set: expected a symbol");
		sy = (int32_t)was_sym(m, s, n);
		n = ident(l, &s);
		if (!n)
			return was_error(m, ".set: expected a symbol");
		t = (int32_t)was_sym(m, s, n);
		m->sym[sy].alias = t;
		m->sym[sy].set = true;
		return 0;
	}
	if (IS(".p2align") || IS(".balign") || IS(".align")) {
		if (!number(l, &v) || v < 0)
			return was_error(m, "%.*s: expected a number", (int)dn, d);
		if (IS(".balign")) {
			int k = 0;

			while ((1ll << k) < v)
				k++;
			v = k;
		}
		if (a->cur == 2) {
			wseg *g = curseg(a);

			if ((uint32_t)v > g->p2align)
				g->p2align = (uint32_t)v;
			while (g->data.n & (((size_t)1 << v) - 1))
				put8(&g->data, 0);
		}
		return 0;
	}
	if (IS(".no_dead_strip")) {
		/* kept by the linker: NO_STRIP (and EXPORTED, for emscripten) */
		const char *t;
		size_t tn = ident(l, &t);
		uint32_t k;

		if (!tn)
			return was_error(m, ".no_dead_strip: expected a symbol");
		k = was_sym(m, t, tn);
		was_reg(m, k);
		m->sym[k].nostrip = true;
		if (tn == 25 && !memcmp(t, "__indirect_function_table", 25))
			m->sym[k].kind = WS_TABLE;
		return 0;
	}
	if (IS(".file") || IS(".ident") || IS(".addrsig") || IS(".addrsig_sym"))
		return 0;
	if (a->cur == 3) {
		/* .init_array: function pointers */
		if (!(IS(".quad") || IS(".int64") || IS(".long") || IS(".int32") || IS(".int")))
			return was_error(m, "%.*s in .init_array", (int)dn, d);
		do {
			if (!expr(m, l, &v, &sy) || sy < 0 || v)
				return was_error(m, ".init_array: expected a function");
			sym_use(a, (uint32_t)sy, WS_FUNCTION);
			m->sym[sy].used_reloc = true;
			GROW(m->init, m->ninit, m->cinit);
			m->init[m->ninit++] = (struct winit){ a->prio, (uint32_t)sy };
		} while (accept(l, ','));
		return 0;
	}
	/* data */
	if (a->cur != 2)
		return was_error(m, "%.*s outside a data section", (int)dn, d);
	if (IS(".byte") || IS(".int8"))
		return data_value(a, l, 1);
	if (IS(".short") || IS(".int16") || IS(".hword"))
		return data_value(a, l, 2);
	if (IS(".long") || IS(".int32") || IS(".int"))
		return data_value(a, l, 4);
	if (IS(".quad") || IS(".int64"))
		return data_value(a, l, 8);
	if (IS(".zero") || IS(".skip") || IS(".space")) {
		int64_t fill = 0;
		vbuf *b = &curseg(a)->data;

		if (!number(l, &v) || v < 0)
			return was_error(m, "%.*s: expected a size", (int)dn, d);
		if (accept(l, ',') && !number(l, &fill))
			return was_error(m, "%.*s: expected a fill byte", (int)dn, d);
		for (int64_t i = 0; i < v; i++)
			put8(b, (uint8_t)fill);
		return 0;
	}
	if (IS(".ascii"))
		return data_string(a, l, false);
	if (IS(".asciz") || IS(".string"))
		return data_string(a, l, true);
#undef IS
	return was_error(m, "unknown directive '%.*s'", (int)dn, d);
}

/* ---------------- statements ---------------- */

static int label(was *a, uint32_t s)
{
	wmod *m = a->m;
	wsym *y = &m->sym[s];

	if (y->defined)
		return was_error(m, "symbol '%s' defined twice", y->name);
	was_reg(m, s);
	y->defined = true;
	if (a->cur == 1) {
		if (a->fn >= 0)
			return was_error(m, "a label inside a function ('%s')", y->name);
		y->kind = WS_FUNCTION;
		a->label = (int32_t)s;
	} else if (a->cur == 2) {
		if (a->custom)
			return was_error(m, "a label in a custom section ('%s')", y->name);
		y->kind = WS_DATA;
		y->seg = a->seg;
		y->off = curseg(a)->data.n;
	} else {
		return was_error(m, "a label outside a section ('%s')", y->name);
	}
	return 0;
}

static int statement(was *a, const char *p, const char *e)
{
	wmod *m = a->m;
	lx l = { p, e };
	const char *s;
	size_t n;

	if (at_end(&l))
		return 0;
	n = ident(&l, &s);
	if (!n)
		return was_error(m, "syntax error: '%.*s'", (int)(e - p), p);
	ws(&l);
	if (l.p < l.e && *l.p == ':') {
		int r;

		l.p++;
		r = label(a, was_sym(m, s, n));
		if (r)
			return r;
		return statement(a, l.p, e);
	}
	if (l.p < l.e && *l.p == '=') {
		/* a = b */
		uint32_t x = was_sym(m, s, n);

		l.p++;
		n = ident(&l, &s);
		if (!n)
			return was_error(m, "expected a symbol after =");
		m->sym[x].alias = (int32_t)was_sym(m, s, n);
		was_reg(m, x);
		return 0;
	}
	if (s[0] == '.')
		return directive(a, s, n, &l);
	{
		const winsn *w = insn_find(s, n);

		if (!w)
			return was_error(m, "unknown instruction '%.*s'", (int)n, s);
		if (a->fn < 0)
			return was_error(m, "instruction '%.*s' outside a function", (int)n, s);
		return instruction(a, w, &l);
	}
}

static void mod_free(wmod *m)
{
	for (uint32_t i = 0; i < m->nsym; i++)
		free(m->sym[i].name);
	for (uint32_t i = 0; i < m->nseg; i++) {
		free(m->seg[i].name);
		free(m->seg[i].data.p);
		free(m->seg[i].rel);
	}
	for (uint32_t i = 0; i < m->nfn; i++) {
		free(m->fn[i].locals.p);
		free(m->fn[i].body.p);
		free(m->fn[i].rel);
	}
	free(m->sym);
	free(m->ht);
	free(m->reg);
	free(m->ci_at);
	free(m->ci_sig);
	free(m->sig);
	free(m->seg);
	free(m->init);
	for (uint32_t i = 0; i < m->ncust; i++) {
		free(m->cust[i].name);
		free(m->cust[i].data.p);
	}
	free(m->cust);
	free(m->fn);
}

int wasm_assemble(const char *text, size_t len, uint8_t **out, size_t *outlen, char *err, size_t errcap)
{
	wmod m = { .err = err, .errcap = errcap };
	was a = { .m = &m, .seg = -1, .fn = -1, .label = -1 };
	const char *p = text, *e = text + len;
	vbuf o = { 0 };
	int r = 0;

	err[0] = 0;
	while (p < e && !r) {
		const char *q = memchr(p, '\n', (size_t)(e - p));
		const char *le = q ? q : e;

		m.line++;
		/* statements separated by ';' (outside strings) */
		{
			const char *st = p;
			bool str = false;

			for (const char *c = p; c < le && !r; c++) {
				if (*c == '"' && (c == p || c[-1] != '\\'))
					str = !str;
				else if (!str && *c == '#')
					break;
				else if (!str && *c == ';') {
					r = statement(&a, st, c);
					st = c + 1;
				}
			}
			if (!r) {
				const char *ce = st;

				str = false;
				while (ce < le) {
					if (*ce == '"' && (ce == st || ce[-1] != '\\'))
						str = !str;
					else if (!str && *ce == '#')
						break;
					ce++;
				}
				r = statement(&a, st, ce);
			}
		}
		p = q ? q + 1 : e;
	}
	m.line = 0;
	if (!r && a.fn >= 0)
		r = was_error(&m, "function '%s' has no end_function", m.sym[m.fn[a.fn].sym].name);
	if (!r)
		r = wasm_write(&m, &o);
	mod_free(&m);
	if (r) {
		free(o.p);
		return -1;
	}
	*out = (uint8_t *)o.p;
	*outlen = o.n;
	return 0;
}
