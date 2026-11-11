#include "types.h"
#include <stdarg.h>
int f(int unused,...){va_list ap;va_start(ap,unused);B x=va_arg(ap,B);va_end(ap);for(int i=0;i<LANES;i++)if(x[i]!=(i%3==0))return 1;return 0;}
