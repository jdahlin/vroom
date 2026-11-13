/* vcc-flags: -std=c23 */
/* #embed takes the file's bytes as they are on disk, not the lexer's copy:
   a leading BOM stays EF BB BF (not three spaces), and in C23 the ' of 1'0
   stays (the lexer drops the separators of numbers) */
#include <stdio.h>

static const unsigned char d[] = {
#embed "embed_raw.bin"
};

int main(void)
{
	printf("%zu:", sizeof d);
	for (unsigned i = 0; i < sizeof d; i++)
		printf(" %u", d[i]);
	printf("\n");
	return 0;
}
