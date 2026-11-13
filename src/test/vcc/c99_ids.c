/* vcc-flags: -std=c99 */
/* the C23 keywords are identifiers before C23 (TODO.md tier 3, C23) */
#include <stdio.h>

int bool = 1, true = 2, false = 3, nullptr = 4, constexpr = 5, alignas = 6, alignof = 7,
    static_assert = 8, thread_local = 9, typeof_unqual = 10;

int main(void)
{
	printf("%d\n", bool + true + false + nullptr + constexpr + alignas + alignof + static_assert +
			       thread_local + typeof_unqual);
	return 0;
}
