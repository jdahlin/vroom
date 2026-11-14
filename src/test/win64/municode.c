/* vcc-flags: -municode */
/* -municode (PLAN-win64.md WIN6): -DUNICODE and the wide start file (crt2u.o, wmain), as clang's
   MinGW driver; the program sees its arguments as wchar_t. */
#include <stdio.h>

int wmain(int argc, unsigned short **argv)
{
	int n = 0;

	for (unsigned short *p = argv[0]; *p; p++)
		n++;
#ifdef UNICODE
	printf("UNICODE %d\n", UNICODE);
#endif
	printf("argc %d, argv[0] has %s\n", argc, n > 0 ? "characters" : "none");
	return 0;
}
