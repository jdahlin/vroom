#ifndef G
#define G
int a = __COUNTER__;
#if __COUNTER__ < 6
#undef G
#include "g.h"
#endif
#endif
