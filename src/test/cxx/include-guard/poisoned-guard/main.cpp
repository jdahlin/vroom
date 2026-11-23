/* the guard poisoned after the first include */
#include "g.h"
#pragma GCC poison G
#include "g.h"
int end = __COUNTER__;
