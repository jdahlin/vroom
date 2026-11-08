#include <stdio.h>
#include "narrow.h"

static int bad;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); bad++; } } while (0)

static signed char mine(int x) { return (signed char)(x * 3); }

int main(void)
{
	volatile int k = 0x1ff, m = -1, big = 0x12345;

	CHECK(retc(k) == -1);
	CHECK(retuc(m) == 255);
	CHECK(adds(30000, 30000) == -5536);
	CHECK(retus(m) == 65535);
	CHECK(retb(k) == 1 && retb(4) == 0);
	CHECK(rete(0x1c8) == S_B);
	CHECK(tailc(0x2fe) == -1);
	CHECK(takes(k, big, k & 4, big) == -1000000 + 0x2345 * 10 + 1 + 0x2345 * 100);
	CHECK(takes_many(1, 2, 3, 4, 5, 6, 7, 8, k, big, m) == 36 - 1000 + 0x2345 * 10 + 255);
	CHECK(via(retc, 0x3ff) == 0);
	CHECK(via(mine, 0x55) == (signed char)0xff + 1);
	CHECK(pass_on(0x1ff) == -1000000 + 511 * 10 + 1 + 511 * 100);
	printf("%d %d %d\n", retc(k), adds(30000, 30000), bad);
	return bad;
}
