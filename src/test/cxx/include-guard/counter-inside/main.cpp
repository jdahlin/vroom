/* a __COUNTER__ inside the guard: a skipped include expands nothing */
#include "g.h"
#include "g.h"
#include "g.h"
int end = __COUNTER__;
