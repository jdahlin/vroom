/* __builtin_X of a library function vcc has no code for: declared with
   clang's prototype (cc/libbuiltins.inc, generated from clang), a call to X
   (TODO.md tier 3, builtins) */
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	char buf[32], *p;
	const char *s = __builtin_strdup("Hello");
	int w[] = { 'a', 'b', 0 };

	p = __builtin_mempcpy(buf, "ab", 2);
	p = __builtin_stpcpy(p, "cd");
	printf("%s %d %d\n", buf, (int)(p - buf), __builtin_bcmp("xy", "xz", 2) != 0);
	printf("%s %d %s\n", s, __builtin_strcasecmp("ABC", "abc"), __builtin_strpbrk("hello", "lo"));
	printf("%g %d\n", __builtin_fmod(7.5, 2.0), (int)__builtin_wcslen(w));
	free((void *)s);
	return 0;
}
