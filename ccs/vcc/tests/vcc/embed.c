/* vcc-flags: -std=c23 */
/* #embed and __has_embed (C23): the bytes, limit, clang::offset, prefix,
   suffix, if_empty; the file embedding itself for a checksum */
#include <stdio.h>

static const unsigned char self[] = {
#embed "embed.c"
};
static const unsigned char d[] = {
#embed "embed_data.bin"
};
static const int lim[] = {
#embed "embed_data.bin" limit(2) prefix(7, ) suffix(, 9)
};
static const int off[] = {
#embed "embed_data.bin" clang::offset(3) limit(2)
};
static const int none[] = {
#embed "embed_data.bin" limit(0) if_empty(-1)
};
#if __has_embed("embed_data.bin") == 1 && __has_embed("nope.bin") == 0 && __has_embed("embed_data.bin" limit(0)) == 2
static const int has = 1;
#else
static const int has = 0;
#endif
#if __has_embed("embed_data.bin" vendor::unknown(1)) == 0
static const int unk = 1;
#else
static const int unk = 0;
#endif

int main(void)
{
	unsigned s = 0;

	for (unsigned i = 0; i < sizeof self; i++)
		s = s * 31 + self[i];
	printf("self %zu %u\n", sizeof self, s);
	for (unsigned i = 0; i < sizeof d; i++)
		printf("%u ", d[i]);
	printf("\nlim %zu: %d %d %d %d\n", sizeof lim / sizeof *lim, lim[0], lim[1], lim[2], lim[3]);
	printf("off %d %d none %d has %d unk %d\n", off[0], off[1], none[0], has, unk);
	return 0;
}
