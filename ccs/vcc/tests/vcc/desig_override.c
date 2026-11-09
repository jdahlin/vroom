/* Designated initializers that override part of an earlier initializer
   (TODO.md tier 1, "Designated-initializer overrides").  The output is
   compared with clang's build, which is the reference for what is kept. */
/* vcc-flags: -Wno-initializer-overrides */
/* vcc-xfail: Designated-initializer overrides differ from clang */
#include <stdio.h>

struct in { int x, y; };
struct out { char s[6]; struct in i; int k; };

static int calls;
static int side(void) { return ++calls; }

static struct out gs = { .s = "ab", .s[3] = 'z', .i = { 1, 2 }, .i.y = 9 };

int main(void)
{
	struct in iv = { side() + 10, side() + 20 };
	struct out a = { .s = "ab", .s[3] = 'z' };
	struct out b = { .i = iv, .i.y = 99 };
	int arr[2] = { [0] = side(), [0] = side() };

	printf("gs %c%c%c%c %d %d\n", gs.s[0], gs.s[1], gs.s[2] ? gs.s[2] : '.', gs.s[3], gs.i.x, gs.i.y);
	printf("a %c%c%c%c\n", a.s[0] ? a.s[0] : '.', a.s[1] ? a.s[1] : '.', a.s[2] ? a.s[2] : '.', a.s[3]);
	printf("b %d %d\n", b.i.x, b.i.y);
	printf("arr %d calls %d\n", arr[0], calls);
	return 0;
}
