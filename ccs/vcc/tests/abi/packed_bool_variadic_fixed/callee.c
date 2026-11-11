#include "types.h"
B fixed(B b, ...) {
 __builtin_va_list ap; __builtin_va_start(ap,b);
 int n=__builtin_va_arg(ap,int); __builtin_va_end(ap);
 b[0]=b[0]^(n==19); return b;
}
