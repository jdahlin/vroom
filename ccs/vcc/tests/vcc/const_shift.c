/* constant shifts by the width or more, or by a negative count: clang folds
   them (with -Wshift-count-overflow) by taking width - 1 and shifting the
   other way for a negative count, so a static initializer stays constant.
   unsigned long is 4 bytes on wasm32, where 1ul << 40 is 1ul << 31 */
#include <stdio.h>
static unsigned long ul = 1ul << 40;
static int i = 1 << 40;
static int neg = 256 >> -2;
static long long sr = -8LL >> 70;
static unsigned long long ur = 0x8000000000000000ull >> 64;
int main(void)
{
	printf("%lu %d %d %lld %llu\n", ul, i, neg, sr, ur);
	return 0;
}
