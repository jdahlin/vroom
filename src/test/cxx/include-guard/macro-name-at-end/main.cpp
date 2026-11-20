/* a token after the #endif (a function-like macro name the includer calls): not a guard */
#include "g.h"
(1)
#include "g.h"
(2)
int end = __COUNTER__;
