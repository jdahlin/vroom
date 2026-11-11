/* abi-targets: linux; Wasm assembler signatures currently hold at most 1020 types. */
#include "types.h"
int main(void) {
{ B4096 a={},b={}; for(int i=0;i<4096;i++){a[i]=i%3==0;b[i]=i%2==0;}
B4096 r=flip4096(a,19,b); for(int i=0;i<4096;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 33;
r=pressure4096(1,2,3,4,5,6,7,8,a);for(int i=0;i<4096;i++)if(r[i]!=(i%3==0))return 2; }
return 0;
}
