/* a self-include under the guard, steered by __COUNTER__ */
#include "g.h"
#include "g.h"
#undef G
#include "g.h"
#include "g.h"
int end = __COUNTER__;
