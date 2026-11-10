/* __builtin_memcpy/memmove/memset with a constant size are inline stores, as
   clang's at every -O and under -fno-builtin (the vDSO has no memcpy:
   get/put_unaligned in lib/vdso/getrandom.c).  Checks the bytes for each
   size up to the inline limit and past it, memset with zero and nonzero
   bytes, memmove's overlap, the returned dst, and arguments with side
   effects evaluated once. */
/* vcc-flags: -fno-builtin */
#include <stdlib.h>

static unsigned char src[200], dst[208], ref[208];

static void fill(void)
{
	for (int i = 0; i < 200; i++)
		src[i] = (unsigned char)(i * 7 + 3);
	for (int i = 0; i < 208; i++)
		dst[i] = ref[i] = 0xee;
}

static void same(int line)
{
	for (int i = 0; i < 208; i++)
		if (dst[i] != ref[i])
			abort();
	(void)line;
}

#define CPY(n) do { fill(); for (int i = 0; i < (n); i++) ref[3 + i] = src[1 + i];	\
		if (__builtin_memcpy(dst + 3, src + 1, (n)) != dst + 3) abort(); same(__LINE__); } while (0)
#define SET(v, n) do { fill(); for (int i = 0; i < (n); i++) ref[5 + i] = (unsigned char)(v);	\
		if (__builtin_memset(dst + 5, (v), (n)) != dst + 5) abort(); same(__LINE__); } while (0)

int main(void)
{
	CPY(0); CPY(1); CPY(2); CPY(3); CPY(4); CPY(5); CPY(7); CPY(8); CPY(9); CPY(15); CPY(16);
	CPY(31); CPY(63); CPY(127); CPY(128); CPY(129); CPY(200);
	SET(0, 1); SET(0, 7); SET(0, 16); SET(0, 100); SET(0, 129);
	SET(0xab, 1); SET(0xab, 3); SET(0x5a, 8); SET(0x5a, 13); SET(0x1ff, 33); SET(-1, 128); SET(0x80, 150);

	/* memmove: overlapping both ways, inline sizes and others */
	for (int n = 1; n <= 16; n++) {
		unsigned char b[32], r[32];

		for (int i = 0; i < 32; i++)
			b[i] = r[i] = (unsigned char)i;
		for (int i = n - 1; i >= 0; i--)
			r[3 + i] = r[1 + i];
		switch (n) {
		case 1: __builtin_memmove(b + 3, b + 1, 1); break;
		case 2: __builtin_memmove(b + 3, b + 1, 2); break;
		case 3: __builtin_memmove(b + 3, b + 1, 3); break;
		case 4: __builtin_memmove(b + 3, b + 1, 4); break;
		case 8: __builtin_memmove(b + 3, b + 1, 8); break;
		case 16: __builtin_memmove(b + 3, b + 1, 16); break;
		default: __builtin_memmove(b + 3, b + 1, (unsigned)n); break;
		}
		for (int i = 0; i < 32; i++)
			if (b[i] != r[i])
				abort();
	}
	{
		unsigned char b[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };

		if (__builtin_memmove(b, b + 2, 8) != b || b[0] != 3 || b[7] != 10 || b[8] != 9)
			abort();
	}

	/* get/put_unaligned as the kernel spells them */
	{
		unsigned long v = 0x1122334455667788ul, w;
		unsigned char buf[16] = { 0 };

		__builtin_memcpy(buf + 1, &v, sizeof(v));
		__builtin_memcpy(&w, buf + 1, sizeof(w));
		if (w != v || buf[1] != 0x88 || buf[8] != 0x11 || buf[0] || buf[9])
			abort();
	}

	/* each argument evaluated once */
	{
		unsigned char a[8] = { 0 }, *p = a, *q = src;
		int k = 0;

		__builtin_memcpy(p++, q++, 4);
		__builtin_memset(a + (k++, 4), 9, 2);
		if (p != a + 1 || q != src + 1 || k != 1 || a[0] != src[0] || a[3] != src[3] || a[4] != 9 || a[5] != 9 || a[6])
			abort();
	}
	return 0;
}
