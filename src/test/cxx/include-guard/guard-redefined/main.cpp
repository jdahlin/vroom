/* the includer #undefs the guard and redefines it as __COUNTER__ */
#include "g.h"
#undef G
#include "g.h"
#undef G
#define G __COUNTER__
#include "g.h"
int end = __COUNTER__ + G;
