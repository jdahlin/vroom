/* __INCLUDE_LEVEL__ and an #include whose name a macro builds */
#define S(x) #x
#define X(x) S(g##x.h)
#define C1 1
#include X(1)
#include X(1)
#undef G1
#include X(1)
int end = __COUNTER__;
