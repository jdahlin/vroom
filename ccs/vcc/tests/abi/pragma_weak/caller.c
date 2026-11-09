/* vcc-xfail-host: macho.c: no MH_SUBSECTIONS_VIA_SYMBOLS (ld64 cannot drop a
   weak definition of vcc's: "duplicate symbol _wdef") */
#include <stdio.h>
#include "pw.h"

static int bad;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); bad++; } } while (0)

int wdef(void) { return 2; }
int wvar = 11;
int wover(void) { return 8; }

#ifndef __APPLE__	/* ld64 wants every weak reference resolved */
extern int nothere;
#pragma weak nothere
extern void nofn(void);
#pragma weak nofn
#endif

int main(void)
{
	CHECK(wdef() == 2);
	CHECK(wonly() == 3);
	CHECK(wvar == 11);
	CHECK(wvar2 == 20);
	CHECK(wover() == 8);
	CHECK(callee_aliases());
#ifndef __APPLE__
	CHECK(walias() == 7);
	CHECK(wlater() == 9);
	CHECK(valias == 30);
	CHECK(&nothere == 0);
	CHECK(!nofn);
	if (nofn)
		nofn();
#endif
	printf("%d %d %d\n", wdef(), wvar, wover());
	return bad;
}
