/* Many parked wide arguments must be bound one at a time by the inliner. */
typedef unsigned __int128 U;
static int calls;
__attribute__((noinline)) static U input(unsigned i) {
 calls++; return ((U)(i+1)<<100) | (i*17+3);
}
__attribute__((always_inline)) static inline U combine(U a,U b,U c,U d,U e,U f,U g,U h,U i,U j,U k,U l) {
 a+=l; b^=k; c-=j;
 return a^b^c^d^e^f^g^h^i;
}
int main(void) {
 U a[12]; for(unsigned i=0;i<12;i++)a[i]=input(i);
 U want=(a[0]+a[11])^(a[1]^a[10])^(a[2]-a[9])^a[3]^a[4]^a[5]^a[6]^a[7]^a[8];
 calls=0;
 U got=combine(input(0),input(1),input(2),input(3),input(4),input(5),input(6),input(7),input(8),input(9),input(10),input(11));
 if(got!=want||calls!=12)return 1;
 U x=((U)1<<111)+27;
 want=(x+(x+11))^((x+1)^(x+10))^((x+2)-(x+9))^(x+3)^(x+4)^(x+5)^(x+6)^(x+7)^(x+8);
 if(combine(x,x+1,x+2,x+3,x+4,x+5,x+6,x+7,x+8,x+9,x+10,x+11)!=want)return 2;
 return 0;
}
