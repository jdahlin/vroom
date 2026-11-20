/* -Wunused-macros: a skipped #ifndef does not use the main file's macro (as clang) */
#include "g.h"
#undef G
#define G 1
#include "g.h"
int end = __COUNTER__;
