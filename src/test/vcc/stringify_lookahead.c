/*
 * Spellings pp makes (#x, a##b, __LINE__) live in pp's tmp arena, reused
 * when expansion is back at file level.  The parser looks ahead while
 * joining adjacent string literals, so a second stringification used to
 * overwrite the first: PostgreSQL's psql built "relkind IN ('p','p',"
 * from CppAsString2(RELKIND_RELATION) "," CppAsString2(RELKIND_PARTITIONED_TABLE) ",".
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define RELKIND_RELATION	  'r'	/* same length as the next one */
#define RELKIND_PARTITIONED_TABLE 'p'
#define S(x) #x
#define S2(x) S(x)
#define CAT(a, b) a##b

static const char *const tab[] = {
	S2(RELKIND_RELATION) "," S2(RELKIND_PARTITIONED_TABLE) ",",
	S2(1) S2(2) S2(3) S2(4),			/* four in a row: past the lookahead window */
#line 500
	S2(__LINE__) "-" S2(__LINE__),
	"x" S2(RELKIND_RELATION) "y" S2(RELKIND_PARTITIONED_TABLE),
};

static void check(const char *got, const char *want)
{
	printf("%s\n", got);
	if (strcmp(got, want))
		abort();
}

int main(void)
{
	char buf[64];
	int n = CAT(1, 2) + CAT(3, 4);			/* pasted numbers followed by more pasting */

	check(tab[0], "'r','p',");
	check(tab[1], "1234");
	snprintf(buf, sizeof buf, "%d-%d", 500, 500);
	check(tab[2], buf);
	check(tab[3], "x'r'y'p'");
	check(S2(RELKIND_RELATION) "," S2(RELKIND_PARTITIONED_TABLE), "'r','p'");
	printf("%d\n", n);
	if (n != 46)
		abort();
	return 0;
}
