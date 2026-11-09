/* vcc-flags: -std=c89 */
/* C89 has no line comments: the slashes below are two divisions around a block comment */
int printf(const char *, ...);

int main(void)
{
	int x = 6 //**/ 2
		;
	const char *s = "//";

	printf("%d %s\n", x, s);
	return x != 3;
}
