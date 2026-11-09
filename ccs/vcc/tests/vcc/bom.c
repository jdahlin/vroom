#define FIRST_LINE 4
/* a UTF-8 byte order mark starts this file and bom_inc.h: not text, so the
   directives on their first lines work (TODO.md tier 3, BOM) */
#include <stdio.h>
#include "bom_inc.h"

int main(void)
{
	printf("%d %d\n", FIRST_LINE, FROM_HEADER);
	return FIRST_LINE + FROM_HEADER != 7;
}
