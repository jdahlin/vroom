#include <string.h>

#define NOINLINE __attribute__((noinline))

static NOINLINE int word(const char *p) { return memcmp(p, ".req", 4) == 0; }
static NOINLINE int reversed(const char *p) { return memcmp("a\0cdefgh", p, 8) != 0; }
static NOINLINE int ordered(const char *p) { return memcmp(p, "ab", 2) < 0; }
static NOINLINE int narrowed(const char *p) { return (unsigned char)memcmp(p, "ab", 2) == 0; }
static NOINLINE int changed(char *p)
{
	int result = memcmp(p, "ab", 2);
	*p = 'x';
	return result == 0;
}

/* Exercise every folded width, high bits, both literal positions and the
 * direct branch form without depending on memcmp's nonzero magnitude. */
#define CHECK_WIDTH(n) \
static NOINLINE int equal##n(const char *p) { return memcmp(p, "\x80\xff\0defgh", n) == 0; } \
static NOINLINE int branch##n(const char *p) { \
	if (memcmp("\x80\xff\0defgh", p, n)) return 7; \
	return 3; \
}
CHECK_WIDTH(1)
CHECK_WIDTH(2)
CHECK_WIDTH(4)
CHECK_WIDTH(8)

int main(void)
{
	char bytes[] = "\x80\xff\0defgh";
	for (unsigned i = 0; i < 8; i++) {
		bytes[i] ^= 1;
#define CHECK(n) if (equal##n(bytes) != (i >= n) || branch##n(bytes) != (i >= n ? 3 : 7)) return 3;
		CHECK(1)
		CHECK(2)
		CHECK(4)
		CHECK(8)
#undef CHECK
		bytes[i] ^= 1;
	}
	char buf[] = "xab";
	char wide[] = "xa\0cdefgh";
	if (!word("x.req" + 1) || word(".reQ") || reversed(wide + 1))
		return 1;
	wide[8] = 'H';
	return !reversed(wide + 1) || !ordered("aa") || ordered("ac") ||
	       !narrowed("ab") || !changed(buf + 1) || buf[1] != 'x';
}
