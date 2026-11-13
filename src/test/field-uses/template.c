/* select: One.count */
/* select: Two.count */
struct One { int count; };
struct Two { int count; };
#define TPL(x) one_##x
#define BOX One
#include "template.h"
#undef TPL
#undef BOX
#define TPL(x) two_##x
#define BOX Two
#include "template.h"
