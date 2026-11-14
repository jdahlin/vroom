/* vcc-flags: -mwindows */
/* -mwindows (PLAN-win64.md WIN6): the GUI subsystem in the image's optional header and gdi32/comdlg32
   on the link line, as clang's MinGW driver; main is still the entry's callee (MinGW's crt2). */
#include <stdio.h>

extern unsigned char __ImageBase[];
__declspec(dllimport) int GetDeviceCaps(void *dc, int index);	/* gdi32: linked by -mwindows */
static int (*volatile gdi)(void *, int) = GetDeviceCaps;

int main(void)
{
	unsigned pe = *(unsigned *)(__ImageBase + 0x3c);
	unsigned short subsystem = *(unsigned short *)(__ImageBase + pe + 24 + 68);

	printf("subsystem %u, gdi32 %s\n", subsystem, gdi ? "linked" : "missing");
	return subsystem == 2 ? 0 : 1;
}
