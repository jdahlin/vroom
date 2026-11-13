/*
 * Byte buffers, gathered from the components that each had their own.  They
 * differ in what happens when memory runs out and in how they are stored, so
 * they are still separate types; making them one is a later step.
 *
 *   vbuf    malloc'ed, doubling, aborts when realloc fails (pp diagnostics,
 *           fixdep output)
 *   vsbuf   malloc'ed, doubling, a failed realloc sets oom and later puts do
 *           nothing (ar inside vccd: a request fails, the server stays up)
 *   vzbuf   malloc'ed, doubling, keeps a NUL after the bytes, put returns
 *           false when realloc fails (rsh inside vccd)
 *   vfixbuf a fixed 4096-byte array, truncating, NUL-terminated (cc's C
 *           spellings of types for the layout checks)
 *   vfdbuf  a caller-supplied array flushed to an fd when nearly full (cc's
 *           layout checks)
 *
 * The emitter's text buffer, a fixed mmap reservation, is tbuf.h.
 */
#ifndef VCC_COMMON_BUFFER_H
#define VCC_COMMON_BUFFER_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/attributes.h"

/* ---------------- vbuf ---------------- */

typedef struct vbuf {
	char *p;
	size_t n, cap;
} vbuf;

static inline VNONNULL(1) void vbuf_put(vbuf *b, const void *s, size_t n)
{
	if (b->n + n > b->cap) {
		size_t c = b->cap ? b->cap * 2 : 4096;

		while (c < b->n + n)
			c *= 2;
		b->p = realloc(b->p, c);
		if (!b->p)
			abort();
		b->cap = c;
	}
	memcpy(b->p + b->n, s, n);
	b->n += n;
}

static inline void vbuf_s(vbuf *b, const char *s) { vbuf_put(b, s, strlen(s)); }

__attribute__((format(printf, 2, 3)))
static inline void vbuf_f(vbuf *b, const char *fmt, ...)
{
	char t[1024];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(t, sizeof(t), fmt, ap);
	va_end(ap);
	vbuf_put(b, t, n < (int)sizeof(t) ? (size_t)n : sizeof(t) - 1);
}

/* ---------------- vsbuf ---------------- */

typedef struct vsbuf {
	char *p;
	size_t n, cap;
	bool oom;
} vsbuf;

static inline VNONNULL(1) void vsbuf_put(vsbuf *b, const void *s, size_t n)
{
	if (b->oom)
		return;
	if (b->n + n > b->cap) {
		size_t cap = b->cap ? b->cap : 4096;
		char *p;

		while (cap < b->n + n)
			cap *= 2;
		if (!(p = realloc(b->p, cap))) {
			b->oom = true;
			return;
		}
		b->p = p;
		b->cap = cap;
	}
	memcpy(b->p + b->n, s, n);
	b->n += n;
}

static inline void vsbuf_str(vsbuf *b, const char *s)
{
	vsbuf_put(b, s, strlen(s));
}

/* ---------------- vzbuf ---------------- */

typedef struct vzbuf {
	char *p;
	size_t n, cap;
} vzbuf;

static inline VUSE VNONNULL(1) bool vzbuf_put(vzbuf *b, const char *s, size_t n)
{
	if (b->n + n + 1 > b->cap) {
		size_t cap = b->cap ? b->cap : 256;
		char *p;

		while (b->n + n + 1 > cap)
			cap *= 2;
		p = realloc(b->p, cap);
		if (!p)
			return false;
		b->p = p;
		b->cap = cap;
	}
	memcpy(b->p + b->n, s, n);
	b->n += n;
	b->p[b->n] = 0;
	return true;
}

/* ---------------- vfixbuf ---------------- */

typedef struct vfixbuf {
	char s[4096];
	size_t n;
} vfixbuf;

static inline void vfixbuf_put(vfixbuf *b, const char *s, size_t n)
{
	if (b->n + n >= sizeof(b->s))
		n = sizeof(b->s) - 1 - b->n;
	memcpy(b->s + b->n, s, n);
	b->n += n;
	b->s[b->n] = 0;
}

static inline void vfixbuf_puts(vfixbuf *b, const char *s) { vfixbuf_put(b, s, strlen(s)); }

/* ---------------- vfdbuf ---------------- */

typedef struct vfdbuf {
	char *p;
	size_t n, cap;
	int fd;
} vfdbuf;

static inline void vfdbuf_flush(vfdbuf *o)
{
	size_t off = 0;

	while (off < o->n) {
		ssize_t w = write(o->fd, o->p + off, o->n - off);

		if (w <= 0)
			break;
		off += (size_t)w;
	}
	o->n = 0;
}

__attribute__((format(printf, 2, 3)))
static inline void vfdbuf_printf(vfdbuf *o, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (o->cap - o->n < 4096)
		vfdbuf_flush(o);
	va_start(ap, fmt);
	n = vsnprintf(o->p + o->n, o->cap - o->n, fmt, ap);
	va_end(ap);
	if (n > 0)
		o->n += (size_t)n < o->cap - o->n ? (size_t)n : o->cap - o->n - 1;
}

#endif
