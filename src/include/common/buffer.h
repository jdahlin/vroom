/*
 * vbuffer: growable bytes (and text), the one byte buffer of the tools.
 *
 * Append with vbuffer_append and its string and format forms; the bytes are
 * buffer.p[0 .. buffer.n), followed by a NUL that is not counted.  A zero
 * buffer (`vbuffer b = { 0 }`) is empty and ready: it grows by doubling from
 * 4096 bytes on the first append.  free(b.p) gives the storage back.
 *
 * What happens when an append cannot be made (out of memory, a fixed buffer
 * full, a write error) is the buffer's flags:
 *   (none)           the job fails (common/fail.h: vout_of_memory, vjob_fail):
 *                    for code that cannot continue without the bytes
 *   VBUFFER_STICKY   `failed` is set and every later append does nothing and
 *                    returns false: for code in a scope that holds a VAUTO*
 *                    or VLOCK resource, or in the server, where a request must
 *                    fail but the process must not (ar, rsh inside vccd);
 *                    the caller checks `failed` (or the result) once at the end
 * Orthogonal to it, where the bytes live:
 *   VBUFFER_FIXED    the storage is the caller's (vbuffer_fixed): the buffer
 *                    never grows, an append that does not fit fails
 *   VBUFFER_FD       the storage is the caller's and is written to fd when the
 *                    next append does not fit, and by vbuffer_flush
 *                    (vbuffer_fd); a write error fails
 *
 * Fields keep short names (p, n, cap): they are read at about 500 sites.
 *
 * The emitter's text buffer, a fixed mmap reservation that is appended to
 * without a bounds check per byte, is text-buffer.h: its appends are the hot
 * path of the code generator and take no flag.
 */
#ifndef VCC_COMMON_BUFFER_H
#define VCC_COMMON_BUFFER_H

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/attributes.h"
#include "common/fail.h"

enum { VBUFFER_STICKY = 1, VBUFFER_FIXED = 2, VBUFFER_FD = 4 };
#define VBUFFER_STICKY_INIT { .flags = VBUFFER_STICKY }

typedef struct vbuffer {
	char *p;
	size_t n, cap;	/* bytes used (without the NUL), bytes of storage */
	int fd;		/* VBUFFER_FD: where the bytes go */
	unsigned char flags;
	bool failed;	/* an append failed (VBUFFER_STICKY: later appends do nothing) */
} vbuffer;

/* a buffer over `size` bytes of the caller's storage; fails when more is appended */
static inline VNONNULL(1, 2) void vbuffer_fixed(vbuffer *b, char *storage, size_t size)
{
	b->p = storage;
	b->n = 0;
	b->cap = size;
	b->fd = -1;
	b->flags = VBUFFER_FIXED;
	b->failed = false;
	storage[0] = 0;
}

/* the same, flushed to fd whenever the next append does not fit */
static inline VNONNULL(1, 2) void vbuffer_fd(vbuffer *b, char *storage, size_t size, int fd)
{
	vbuffer_fixed(b, storage, size);
	b->fd = fd;
	b->flags |= VBUFFER_FD;
}

/* a failure: the flag, and the job unless the buffer is sticky (what: errno's text is added when `with_errno`) */
__attribute__((noinline, cold)) static inline void vbuffer_fail(vbuffer *b, const char *what, bool with_errno)
{
	b->failed = true;
	if (b->flags & VBUFFER_STICKY)
		return;
	if (with_errno)
		vjob_fail_errno(what);
	vjob_fail(what);
}

/* write the bytes out (VBUFFER_FD); a write error fails the buffer */
static inline void vbuffer_flush(vbuffer *b)
{
	size_t off = 0;

	if (b->failed || !(b->flags & VBUFFER_FD))
		return;
	while (off < b->n) {
		ssize_t w = write(b->fd, b->p + off, b->n - off);

		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0) {
			vbuffer_fail(b, "write error", true);
			return;
		}
		off += (size_t)w;
	}
	b->n = 0;
	b->p[0] = 0;
}

/* make room for `extra` more bytes and the NUL (it flushes, grows by doubling, or fails: see the flags) */
__attribute__((noinline)) static inline bool vbuffer_make_room(vbuffer *b, size_t extra)
{
	size_t cap;
	char *p;

	if (b->flags & VBUFFER_FD) {
		vbuffer_flush(b);
		if (b->failed)
			return false;
		if (extra + 1 <= b->cap)
			return true;
		vbuffer_fail(b, "a line is longer than the output buffer", false);
		return false;
	}
	if (b->flags & VBUFFER_FIXED) {
		vbuffer_fail(b, "text too long for a fixed buffer", false);
		return false;
	}
	cap = b->cap ? b->cap * 2 : 4096;
	while (cap < b->n + extra + 1)
		cap *= 2;
	p = realloc(b->p, cap);
	if (!p) {
		b->failed = true;
		if (!(b->flags & VBUFFER_STICKY))
			vout_of_memory();
		return false;
	}
	b->p = p;
	b->cap = cap;
	return true;
}

static inline VNONNULL(1) bool vbuffer_reserve(vbuffer *b, size_t extra)
{
	if (b->failed)
		return false;
	if (__builtin_expect(b->n + extra + 1 <= b->cap, 1))
		return true;
	return vbuffer_make_room(b, extra);
}

/* append n bytes (n may be 0: the buffer then has storage and a NUL); false when the buffer has failed */
static inline VNONNULL(1) bool vbuffer_append(vbuffer *b, const void *s, size_t n)
{
	if (!vbuffer_reserve(b, n))
		return false;
	if (n)	/* (an empty append to an empty buffer: b->p is NULL, which memcpy must not get) */
		memcpy(b->p + b->n, s, n);
	b->n += n;
	b->p[b->n] = 0;
	return true;
}

static inline bool vbuffer_string_append(vbuffer *b, const char *s) { return vbuffer_append(b, s, strlen(s)); }

__attribute__((format(printf, 2, 3)))
static inline bool vbuffer_format_append(vbuffer *b, const char *fmt, ...)
{
	va_list ap, again;
	size_t room = b->failed || !b->p ? 0 : b->cap - b->n;
	int len;

	va_start(ap, fmt);
	va_copy(again, ap);
	len = vsnprintf(room ? b->p + b->n : NULL, room, fmt, ap);	/* the whole length: nothing is cut */
	va_end(ap);
	if (len < 0 || b->failed) {
		va_end(again);
		return false;
	}
	if ((size_t)len < room) {
		b->n += (size_t)len;
	} else if (vbuffer_reserve(b, (size_t)len)) {
		vsnprintf(b->p + b->n, (size_t)len + 1, fmt, again);
		b->n += (size_t)len;
	} else {
		va_end(again);
		return false;
	}
	va_end(again);
	return true;
}

#endif
