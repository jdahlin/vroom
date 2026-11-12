/* C99 6.7.4p7: an inline definition is an external definition when any
 * file-scope declaration of the function lacks 'inline' (mimalloc's bitmap.c,
 * CPython's obmalloc.c).  Both orders; the call goes through a volatile
 * pointer, so the symbol must be emitted for the link to succeed. */
#include <stdio.h>
int before(int);
inline int before(int x) { return x + 1; }
inline int after(int x) { return x + 2; }
int after(int);
extern inline int ext(int x) { return x + 3; }
int (*volatile pb)(int) = before, (*volatile pa)(int) = after, (*volatile pe)(int) = ext;
int main(void)
{
	printf("%d %d %d\n", pb(1), pa(1), pe(1));
	return 0;
}
