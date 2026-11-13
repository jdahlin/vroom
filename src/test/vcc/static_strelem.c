/* An element of a string literal as a static initializer (clang folds it). */
#include <stdio.h>

static char c = "abc"[1];
char d = *"xyz";
int w = L"ab"[1];
static signed char e = "\xff"[0];
static short s[] = { "hi"[0], "hi"[2] };
struct { char a; int b; } r = { "q"[0], "\x7f"[0] };

int main(void)
{
	printf("%d %d %d %d %d %d %d %d\n", c, d, w, e, s[0], s[1], r.a, r.b);
	return 0;
}
