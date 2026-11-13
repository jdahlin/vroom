/* Large initializers stay linear: 262144 plain elements, 65536 designated
   "[i].b = ..." elements (a sub-object each) and a string split by a
   designator (il_run once dropped covered runs with a scan per element:
   PostgreSQL's gram.c took 247 G instructions).  vcc must compile it within
   the vcc-timeout; values compared with clang's build. */
/* vcc-flags: -Wno-initializer-overrides */
/* vcc-timeout: 10 */
#include <stdio.h>
#define A(x) x, x + 1, x + 2, x + 3,
#define B(x) A(x) A(x + 4) A(x + 8) A(x + 12)
#define C(x) B(x) B(x + 16) B(x + 32) B(x + 48)
#define D(x) C(x) C(x + 64) C(x + 128) C(x + 192)
#define E(x) D(x) D(x + 256) D(x + 512) D(x + 768)
#define F(x) E(x) E(x + 1024) E(x + 2048) E(x + 3072)
#define G(x) F(x) F(x + 4096) F(x + 8192) F(x + 12288)
#define H(x) G(x) G(x + 16384) G(x + 32768) G(x + 49152)
#define I(x) H(x) H(x + 65536) H(x + 131072) H(x + 196608)
static const int big[] = { I(0) };

struct p { short a, b; };
#define DA(x) [__COUNTER__].b = x, [__COUNTER__].b = x, [__COUNTER__].b = x, [__COUNTER__].b = x,
#define DB(x) DA(x) DA(x) DA(x) DA(x)
#define DC(x) DB(x) DB(x) DB(x) DB(x)
#define DD(x) DC(x) DC(x) DC(x) DC(x)
#define DE(x) DD(x) DD(x) DD(x) DD(x)
#define DF(x) DE(x) DE(x) DE(x) DE(x)
#define DG(x) DF(x) DF(x) DF(x) DF(x)
#define DH(x) DG(x) DG(x) DG(x) DG(x)
static struct p des[] = { DH(3) [7].a = 9, [70000 - 1].a = 1 };

#define SA "abcdefghijklmnop"
#define SB SA SA SA SA
#define SC SB SB SB SB
#define SD SC SC SC SC
#define SE SD SD SD SD
static struct { char s[65536]; } str = { .s = SE SE SE SE, .s[65000] = 'Z' };

int main(void)
{
	unsigned long sum = 0, sd = 0, ss = 0;

	for (unsigned i = 0; i < sizeof big / sizeof big[0]; i++)
		sum += (unsigned)big[i] * (i | 1);
	for (unsigned i = 0; i < sizeof des / sizeof des[0]; i++)
		sd += (unsigned)(des[i].a + 3 * des[i].b) * (i | 1);
	for (unsigned i = 0; i < sizeof str.s; i++)
		ss += (unsigned char)str.s[i] * (i | 1);
	printf("big %zu %lu des %zu %lu str %lu %c\n", sizeof big / sizeof big[0], sum,
	       sizeof des / sizeof des[0], sd, ss, str.s[65000]);
	return 0;
}
