#include "pw.h"

/* (above the pragmas: after one, clang finds a call to the alias ambiguous) */
int callee_aliases(void)
{
	return walias() == 7 && wlater() == 9 && valias == 30;
}

#pragma weak wdef
int wdef(void) { return 1; }

int wonly(void);
#pragma weak wonly
int wonly(void) { return 3; }

#pragma weak wvar
int wvar = 10;
#pragma weak wvar2
int wvar2 = 20;

int impl(void) { return 7; }
#pragma weak walias = impl
#pragma weak wover = impl

#pragma weak wlater = impl2
int impl2(void) { return 9; }

int vimpl = 30;
#pragma weak valias = vimpl
