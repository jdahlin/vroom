/* #include of a guarded header inside macro arguments */
#define ID(x) x
#include "g.h"
ID(
#include "g.h"
)
int end = __COUNTER__;
