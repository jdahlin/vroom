/* Unit test of common/buffer.h: a zero buffer grows from 4096 bytes; a buffer that starts in the caller's bytes (vbuffer_inline) stays there while it fits and moves to the heap, contents kept, when it does not. */
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "common/buffer.h"

int main(void)
{
	/* a zero buffer: the heap from the first append */
	vbuffer z = {0};
	CHECK(vbuffer_append(&z, "abc", 3));
	CHECK(z.n == 3 && z.cap == 4096 && !strcmp(z.p, "abc") && !z.flags);
	free(z.p);

	/* inline: empty and terminated before any append */
	char bytes[16];
	vbuffer b;
	memset(bytes, 'x', sizeof bytes);
	vbuffer_inline(&b, bytes, sizeof bytes);
	CHECK(b.p == bytes && b.n == 0 && b.cap == sizeof bytes && b.p[0] == 0);
	CHECK(b.flags == VBUFFER_INLINE && !b.failed);

	/* 15 bytes and the NUL fit exactly: still the caller's bytes */
	CHECK(vbuffer_append(&b, "0123456789", 10));
	CHECK(vbuffer_append(&b, "abcde", 5));
	CHECK(b.p == bytes && b.n == 15 && (b.flags & VBUFFER_INLINE));
	CHECK(!strcmp(bytes, "0123456789abcde"));

	/* one more: the heap, with what was there, and the flag clear */
	CHECK(vbuffer_append(&b, "f", 1));
	CHECK(b.p != bytes && b.n == 16 && b.cap == 4096 && !(b.flags & VBUFFER_INLINE));
	CHECK(!strcmp(b.p, "0123456789abcdef"));
	CHECK(!strcmp(bytes, "0123456789abcde")); /* the caller's bytes are left alone */

	/* from then on it grows as any heap buffer */
	for (int i = 0; i < 1000; i++)
		CHECK(vbuffer_format_append(&b, "%08d", i));
	CHECK(b.n == 16 + 8000 && b.cap == 8192 && !strncmp(b.p + 16, "00000000" "00000001", 16));
	CHECK(!strcmp(b.p + b.n - 8, "00000999"));
	free(b.p);

	/* one append larger than the caller's bytes and than the first heap size */
	char big[10000];
	memset(big, 'q', sizeof big);
	vbuffer_inline(&b, bytes, sizeof bytes);
	CHECK(vbuffer_append(&b, "ab", 2));
	CHECK(vbuffer_append(&b, big, sizeof big));
	CHECK(b.p != bytes && b.n == 10002 && b.cap == 16384 && !memcmp(b.p, "abqq", 4) && b.p[10001] == 'q' && b.p[10002] == 0);
	free(b.p);

	/* an empty append gives storage and a NUL without leaving the caller's bytes */
	vbuffer_inline(&b, bytes, sizeof bytes);
	CHECK(vbuffer_append(&b, "", 0) && b.p == bytes && b.n == 0 && b.p[0] == 0);
	return check_failures;
}
