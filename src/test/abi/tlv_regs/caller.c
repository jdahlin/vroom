#include <stdio.h>
#include "tlv.h"

static int bad;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); bad++; } } while (0)

void reset(void);

int main(void)
{
	struct st *s;

	reset();
	s = take(1);	/* made: the -1 check runs */
	CHECK(s && seen_a() == s->a && seen_b() == s->b && seen_n() == 7);
	CHECK(take(0) == s);
	reset();
	s = take(0);	/* from the list */
	CHECK(s && seen_a() == s->a && seen_b() == s->b && seen_n() == 3);
	printf("%ld %ld %d\n", seen_n(), (long)(seen_b() - seen_a()), bad);
	return bad;
}
