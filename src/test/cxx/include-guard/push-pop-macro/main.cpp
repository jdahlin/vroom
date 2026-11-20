/* push_macro/pop_macro of the guard around includes */
#include "g.h"
#pragma push_macro("G")
#undef G
#include "g.h"
#pragma pop_macro("G")
#include "g.h"
#pragma push_macro("G")
#undef G
#pragma pop_macro("G")
#include "g.h"
int end = __COUNTER__;
