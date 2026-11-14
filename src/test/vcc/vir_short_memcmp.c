#include <string.h>

#define NOINLINE __attribute__((noinline))

static NOINLINE int word(const char *p) { return memcmp(p, ".req", 4) == 0; }
static NOINLINE int reversed(const char *p) { return memcmp("a\0cdefgh", p, 8) != 0; }
static NOINLINE int ordered(const char *p) { return memcmp(p, "ab", 2) < 0; }
static NOINLINE int changed(char *p)
{
	int result = memcmp(p, "ab", 2);
	*p = 'x';
	return result == 0;
}

int main(void)
{
	char buf[] = "xab";
	char wide[] = "xa\0cdefgh";
	if (!word("x.req" + 1) || word(".reQ") || reversed(wide + 1))
		return 1;
	wide[8] = 'H';
	return !reversed(wide + 1) || !ordered("aa") || ordered("ac") ||
	       !changed(buf + 1) || buf[1] != 'x';
}
