/* #pragma weak, both forms (TODO.md tier 1, "#pragma weak ignored").  An
   undefined weak symbol has address 0 at run time; the compiler may not fold
   &ext != 0 to 1. */
/* vcc-xfail: #pragma weak ignored */
#include <stdio.h>

extern int ext;
#pragma weak ext
extern void missing(void);
#pragma weak missing

int target(void) { return 42; }
#pragma weak alias = target
int alias(void);

int main(void)
{
	int bad = 0;

	bad |= &ext != 0;
	bad |= missing != 0;
	if (missing)
		missing();
	bad |= alias() != 42;
	printf("%d %d %d\n", &ext != 0, missing != 0, alias());
	return bad;
}
