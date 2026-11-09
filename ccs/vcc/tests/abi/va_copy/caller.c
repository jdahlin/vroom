#include <stdio.h>
#include "va.h"

static int bad;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); bad++; } } while (0)

int main(void)
{
	CHECK(sum(3, 1L, 2L, 3L) == 24);
	CHECK(sum(0) == 0);
	CHECK(sum_twice(2, 5L, 7L) == 120 + 48);
	printf("%ld %ld %d\n", sum(3, 1L, 2L, 3L), sum_twice(2, 5L, 7L), bad);
	return bad;
}
