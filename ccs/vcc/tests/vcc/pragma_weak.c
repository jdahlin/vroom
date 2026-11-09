/* #pragma weak, both forms (TODO.md tier 1, "#pragma weak ignored").  An
   undefined weak symbol has address 0 at run time; the compiler may not fold
   &ext != 0 to 1, whatever the form of the test and whether the symbol is
   weak by pragma or by attribute.  Bindings: tests/abi/pragma_weak. */
#include <stdio.h>

extern int ext;
#pragma weak ext
extern void missing(void);
#pragma weak missing
#pragma weak later		/* before the declaration */
extern int later;
extern int attr __attribute__((weak));
extern int arr[] __attribute__((weak));

int target(void) { return 42; }
#pragma weak alias = target
int alias(void);
#pragma weak alias2 = target2	/* the target declared after the pragma */
int target2(void) { return 43; }
int alias2(void);

static int (*fp)(void) = alias;

int main(void)
{
	int bad = 0;

	bad |= &ext != 0;
	bad |= missing != 0;
	if (missing)
		missing();
	bad |= &later != 0;
	bad |= !!&attr;
	bad |= (&attr ? 1 : 0);
	bad |= &arr[0] != 0;
	bad |= !(!&ext && 1);
	bad |= (_Bool)&missing;
	bad |= alias() != 42;
	bad |= alias2() != 43;
	bad |= fp() != 42;
	printf("%d %d %d %d %d\n", &ext != 0, missing != 0, !&attr, alias(), alias2());
	return bad;
}
