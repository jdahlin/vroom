/* #include inside macro arguments: an error, as clang */
#define F(x) x
int a = F(
#include <stddef.h>
1);
