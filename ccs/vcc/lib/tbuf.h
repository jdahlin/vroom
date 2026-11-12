/*
 * tbuf: the code generator's text buffer.  Its bytes are a fixed reservation
 * (an mmap, or a reused vmreg region from arena.h), so a put only checks the
 * bound; running past it aborts.  The fast paths are inline: the printer
 * calls them for every operand.
 */
#ifndef VCC_TBUF_H
#define VCC_TBUF_H

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "arena.h"

typedef struct tbuf {
	char *p;
	size_t n, cap;
} tbuf;

static inline void tb_init(tbuf *b, size_t cap)
{
	b->p = vreserve(cap, MAP_NORESERVE, "vcc: mmap");
	b->n = 0;
	b->cap = cap;
}

/* a buffer in a reused region (arena.h) */
static inline void tb_reg(tbuf *b, vmreg *r, size_t cap, const char *name)
{
	b->p = vmreg_get(r, cap, name);
	b->n = 0;
	b->cap = cap;
}

static inline VNONNULL(1) void tb_need(tbuf *b, size_t n)
{
	if (__builtin_expect(b->n + n > b->cap, 0)) {
		fputs("vcc: emitter buffer overflow\n", stderr);
		abort();
	}
}

static inline VNONNULL(1) void tb_put(tbuf *b, const char *s, size_t n)
{
	tb_need(b, n);
	memcpy(b->p + b->n, s, n);
	b->n += n;
}

static inline VNONNULL(1) void tb_c(tbuf *b, char ch)
{
	tb_need(b, 1);
	b->p[b->n++] = ch;
}

static inline void tb_s(tbuf *b, const char *s) { tb_put(b, s, strlen(s)); }

static inline void tb_u_slow(tbuf *b, uint64_t v)
{
	char t[24];
	int i = 24;

	do
		t[--i] = (char)('0' + v % 10);
	while ((v /= 10));
	tb_put(b, t + i, (size_t)(24 - i));
}

static inline void tb_u(tbuf *b, uint64_t v)
{
	if (__builtin_expect(v < 10, 1)) {
		tb_c(b, (char)('0' + v));
	} else if (v < 100) {
		tb_need(b, 2);
		b->p[b->n] = (char)('0' + v / 10);
		b->p[b->n + 1] = (char)('0' + v % 10);
		b->n += 2;
	} else {
		tb_u_slow(b, v);
	}
}

static inline void tb_i(tbuf *b, int64_t v)
{
	if (v < 0) {
		tb_c(b, '-');
		tb_u(b, (uint64_t)0 - (uint64_t)v);
	} else {
		tb_u(b, (uint64_t)v);
	}
}

/* lower-case hex digits, no prefix: label numbers */
static inline void tb_x(tbuf *b, uint32_t v)
{
	int n = v ? (35 - __builtin_clz(v)) >> 2 : 1;
	char *p;

	tb_need(b, 8);
	p = b->p + b->n;
	for (int i = n - 1; i >= 0; i--, v >>= 4)
		p[i] = "0123456789abcdef"[v & 15];
	b->n += (size_t)n;
}

/* 0x and lower-case hex digits */
static inline void tb_hex(tbuf *b, uint64_t v)
{
	char t[20];
	int i = 20;

	do
		t[--i] = "0123456789abcdef"[v & 15];
	while ((v >>= 4));
	t[--i] = 'x';
	t[--i] = '0';
	tb_put(b, t + i, (size_t)(20 - i));
}

/* a name from a char[8] table: one 8-byte copy, the length from the first zero byte */
static inline void tb_n8(tbuf *b, const char s[8])
{
	uint64_t w, z;

	memcpy(&w, s, 8);
	tb_need(b, 8);
	memcpy(b->p + b->n, &w, 8);
	z = (w - 0x0101010101010101ull) & ~w & 0x8080808080808080ull;
	b->n += z ? (size_t)(__builtin_ctzll(z) >> 3) : 8;
}

__attribute__((format(printf, 2, 3)))
static inline void tb_f(tbuf *b, const char *fmt, ...)
{
	va_list ap;
	int n;

	tb_need(b, 512);
	va_start(ap, fmt);
	n = vsnprintf(b->p + b->n, b->cap - b->n, fmt, ap);
	va_end(ap);
	b->n += (size_t)n;
}

#endif
