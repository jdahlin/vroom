/* __builtin_os_log_format and _buffer_size (Apple's os/log.h, CPython's
 * pylifecycle.c): the buffer clang writes, byte for byte (expected bytes
 * from clang 23), and the size as an integer constant expression.  On
 * Apple also the macro's string: section("__TEXT,__oslogstring,
 * cstring_literals") is a section of its own (vcc's Mach-O writer). */
/* vcc-flags: -w */
#include <stdio.h>
#include <string.h>

static int fails;

static void check(const char *name, const unsigned char *got, unsigned n, const unsigned char *want, unsigned wn)
{
	if (n != wn || memcmp(got, want, n)) {
		printf("FAIL %s:", name);
		for (unsigned i = 0; i < n; i++)
			printf(" %02x", got[i]);
		printf("\n");
		fails++;
	}
}

#define T(fmt, ...) ({ \
	_Static_assert(__builtin_os_log_format_buffer_size(fmt, ##__VA_ARGS__) == sizeof W, fmt); \
	unsigned char buf[__builtin_os_log_format_buffer_size(fmt, ##__VA_ARGS__)]; \
	memset(buf, 0xee, sizeof buf); \
	if ((void *)__builtin_os_log_format(buf, fmt, ##__VA_ARGS__) != (void *)buf) fails++; \
	check(fmt, buf, sizeof buf, W, sizeof W); })

static int calls;
static int side(void) { return ++calls; }

int main(void)
{
	const char *s = (const char *)0x1122334455667788ull;
	void *p = (void *)0x0102030405060708ull;
	{ static const unsigned char W[] = {0, 0}; T("hello %%"); }
	{ static const unsigned char W[] = {0, 1, 0, 4, 0x44, 0x33, 0x22, 0x11}; T("%d", 0x11223344); }
	{ static const unsigned char W[] = {0, 2, 0, 8, 1, 0, 0, 0, 0, 0, 0, 0, 0, 4, 2, 0, 0, 0}; T("%lld %hhd", 1ll, (char)2); }
	{ static const unsigned char W[] = {2, 1, 0x20, 8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11}; T("%s", s); }
	{ static const unsigned char W[] = {2, 1, 0x22, 8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11}; T("%{public}s", s); }
	{ static const unsigned char W[] = {3, 1, 0x25, 8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11}; T("%{sensitive, public}s", s); }
	{ static const unsigned char W[] = {1, 1, 1, 4, 7, 0, 0, 0}; T("%{public,private}d", 7); }
	{ static const unsigned char W[] = {2, 2, 0x70, 8, 'a', 'b', 'c', 'd', 0, 0, 0, 0, 0x22, 8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
	  T("%{public, mask.abcd}s", s); }
	{ static const unsigned char W[] = {0, 1, 0, 8, 8, 7, 6, 5, 4, 3, 2, 1}; T("%p", p); }
	{ static const unsigned char W[] = {2, 2, 0x12, 4, 0x10, 0, 0, 0, 0x32, 8, 8, 7, 6, 5, 4, 3, 2, 1}; T("%{public}.16P", p); }
	{ static const unsigned char W[] = {3, 3, 0x10, 4, 3, 0, 0, 0, 0x21, 8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0, 4, 9, 0, 0, 0};
	  T("%{private}.*s %d", 3, s, 9); }
	{ static const unsigned char W[] = {2, 3, 0, 4, 2, 0, 0, 0, 0x10, 4, 3, 0, 0, 0, 0x20, 8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
	  T("%*.*s", 2, 3, s); }
	{ static const unsigned char W[] = {0, 2, 0, 8, 0, 0, 0, 0, 0, 0, 0xf8, 0x3f, 0, 4, 'x', 0, 0, 0}; T("%f %c", 1.5f, 'x'); }
	{ static const unsigned char W[] = {2, 1, 0x60, 0}; T("%m"); }
	{ static const unsigned char W[] = {0, 2, 0, 4, 2, 0, 0, 0, 0, 4, 1, 0, 0, 0}; T("%2$d %1$d", 1, 2); }
	{ static const unsigned char W[] = {0, 1, 0, 4, 1, 0, 0, 0}; T("%d %d", 1); }
	/* an argument without a conversion is not evaluated (clang) */
	{ static const unsigned char W[] = {0, 1, 0, 4, 1, 0, 0, 0}; T("%d", side(), side()); }
	if (calls != 1) { printf("FAIL calls %d\n", calls); fails++; }
#ifdef __APPLE__
	{
		__attribute__((section("__TEXT,__oslogstring,cstring_literals"))) static const char str[] = "vcc %d";
		extern const char sbegin[] __asm("section$start$__TEXT$__oslogstring");
		extern const char send[] __asm("section$end$__TEXT$__oslogstring");
		unsigned long a = (unsigned long)str, lo = (unsigned long)sbegin, hi = (unsigned long)send;
		if (!(a >= lo && a < hi)) { printf("FAIL string not in __TEXT,__oslogstring\n"); fails++; }
	}
#endif
	printf("%s\n", fails ? "fail" : "ok");
	return fails != 0;
}
