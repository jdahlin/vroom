#include <stdio.h>
#include "va.h"

static int bad;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); bad++; } } while (0)

int main(void)
{
	/* dirty the outgoing argument area, then pass int 0 where a pointer is read */
	CHECK(take_long(10, -1L, -1L, -1L, -1L, -1L, -1L, -1L, -1L, -1L, -1L) == -10);
#ifdef __APPLE__	/* AAPCS64 leaves the upper half unspecified: clang too */
	CHECK(null_ptr(5, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0) == 0);
#endif
	printf("%d\n", bad);
	return bad;
}
