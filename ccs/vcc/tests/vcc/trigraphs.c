/* vcc-flags: -std=c11 -Wno-trigraphs */
/* Trigraphs are replaced before lexing in strict modes before C23 (clang). */
??=include <string.h>
??=define S(x) ??=x

int printf(const char *, ...);

int main(void)
??<
	int a??(2??) = ??< 5, 6 ??>;
	const char *s = S(??(x??));
	char c = '??/'';
	int q = (1 ??! 2) ??' 7;
	int hidden = 1;
	// a trigraph splice joins the next line to this comment ??/
	hidden = 0;
	if (strcmp(s, "[x]") ??!??! c != '\'' ??!??! a??(1??) != 6 ??!??! q != 4 ??!??! !hidden ??!??! ??-0 != -1)
		return 1;
	if (strcmp("???=", "?#") != 0)
		return 2;
	printf("ok\n");
	return 0;
??>
