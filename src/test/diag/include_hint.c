/* D5.4: a missing standard header.  clang declares the library function with its
   type and names the header in a note; vcc adds the #include as a fix-it, shown as
   a patch line after the includes, and also names the header of types, macros and
   POSIX functions clang has no table for.  "size_t n = ..." declares n (of an
   invalid type): clang reports each later use of n again. */
/* diag-differs: vcc declares the variables of an undeclared type, clang reports their uses */
#include "include_hint.h"

int copy(char *d, const char *s)
{
	size_t n = strlen(s);
	FILE *fp = 0;

	memcpy(d, s, n);
	write(1, d, n);
	return d == NULL || fp != 0;
}
