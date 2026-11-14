/* vcc-skip-win64: clang's own build fails for the target (clang x clang exits 1: <N x i1> variadic) */
#include "types.h"
int main(void){B x={};for(int i=0;i<LANES;i++)x[i]=i%3==0;return f(1,x);}
