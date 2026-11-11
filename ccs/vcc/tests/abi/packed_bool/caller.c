#include "types.h"
int main(void) {
{ B1 a={},b={}; for(int i=0;i<1;i++){a[i]=i%3==0;b[i]=i%2==0;}
B1 r=flip1(a,19,b); for(int i=0;i<1;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 11;
r=pressure1(1,2,3,4,5,6,7,8,a);for(int i=0;i<1;i++)if(r[i]!=(i%3==0))return 2; }
{ B2 a={},b={}; for(int i=0;i<2;i++){a[i]=i%3==0;b[i]=i%2==0;}
B2 r=flip2(a,19,b); for(int i=0;i<2;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 12;
r=pressure2(1,2,3,4,5,6,7,8,a);for(int i=0;i<2;i++)if(r[i]!=(i%3==0))return 2; }
{ B3 a={},b={}; for(int i=0;i<3;i++){a[i]=i%3==0;b[i]=i%2==0;}
B3 r=flip3(a,19,b); for(int i=0;i<3;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 13;
r=pressure3(1,2,3,4,5,6,7,8,a);for(int i=0;i<3;i++)if(r[i]!=(i%3==0))return 2; }
{ B4 a={},b={}; for(int i=0;i<4;i++){a[i]=i%3==0;b[i]=i%2==0;}
B4 r=flip4(a,19,b); for(int i=0;i<4;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 14;
r=pressure4(1,2,3,4,5,6,7,8,a);for(int i=0;i<4;i++)if(r[i]!=(i%3==0))return 2; }
{ B5 a={},b={}; for(int i=0;i<5;i++){a[i]=i%3==0;b[i]=i%2==0;}
B5 r=flip5(a,19,b); for(int i=0;i<5;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 15;
r=pressure5(1,2,3,4,5,6,7,8,a);for(int i=0;i<5;i++)if(r[i]!=(i%3==0))return 2; }
{ B6 a={},b={}; for(int i=0;i<6;i++){a[i]=i%3==0;b[i]=i%2==0;}
B6 r=flip6(a,19,b); for(int i=0;i<6;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 16;
r=pressure6(1,2,3,4,5,6,7,8,a);for(int i=0;i<6;i++)if(r[i]!=(i%3==0))return 2; }
{ B7 a={},b={}; for(int i=0;i<7;i++){a[i]=i%3==0;b[i]=i%2==0;}
B7 r=flip7(a,19,b); for(int i=0;i<7;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 17;
r=pressure7(1,2,3,4,5,6,7,8,a);for(int i=0;i<7;i++)if(r[i]!=(i%3==0))return 2; }
{ B8 a={},b={}; for(int i=0;i<8;i++){a[i]=i%3==0;b[i]=i%2==0;}
B8 r=flip8(a,19,b); for(int i=0;i<8;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 18;
r=pressure8(1,2,3,4,5,6,7,8,a);for(int i=0;i<8;i++)if(r[i]!=(i%3==0))return 2; }
{ B9 a={},b={}; for(int i=0;i<9;i++){a[i]=i%3==0;b[i]=i%2==0;}
B9 r=flip9(a,19,b); for(int i=0;i<9;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 19;
r=pressure9(1,2,3,4,5,6,7,8,a);for(int i=0;i<9;i++)if(r[i]!=(i%3==0))return 2; }
{ B15 a={},b={}; for(int i=0;i<15;i++){a[i]=i%3==0;b[i]=i%2==0;}
B15 r=flip15(a,19,b); for(int i=0;i<15;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 20;
r=pressure15(1,2,3,4,5,6,7,8,a);for(int i=0;i<15;i++)if(r[i]!=(i%3==0))return 2; }
{ B16 a={},b={}; for(int i=0;i<16;i++){a[i]=i%3==0;b[i]=i%2==0;}
B16 r=flip16(a,19,b); for(int i=0;i<16;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 21;
r=pressure16(1,2,3,4,5,6,7,8,a);for(int i=0;i<16;i++)if(r[i]!=(i%3==0))return 2; }
{ B17 a={},b={}; for(int i=0;i<17;i++){a[i]=i%3==0;b[i]=i%2==0;}
B17 r=flip17(a,19,b); for(int i=0;i<17;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 22;
r=pressure17(1,2,3,4,5,6,7,8,a);for(int i=0;i<17;i++)if(r[i]!=(i%3==0))return 2; }
{ B31 a={},b={}; for(int i=0;i<31;i++){a[i]=i%3==0;b[i]=i%2==0;}
B31 r=flip31(a,19,b); for(int i=0;i<31;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 23;
r=pressure31(1,2,3,4,5,6,7,8,a);for(int i=0;i<31;i++)if(r[i]!=(i%3==0))return 2; }
{ B32 a={},b={}; for(int i=0;i<32;i++){a[i]=i%3==0;b[i]=i%2==0;}
B32 r=flip32(a,19,b); for(int i=0;i<32;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 24;
r=pressure32(1,2,3,4,5,6,7,8,a);for(int i=0;i<32;i++)if(r[i]!=(i%3==0))return 2; }
{ B33 a={},b={}; for(int i=0;i<33;i++){a[i]=i%3==0;b[i]=i%2==0;}
B33 r=flip33(a,19,b); for(int i=0;i<33;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 25;
r=pressure33(1,2,3,4,5,6,7,8,a);for(int i=0;i<33;i++)if(r[i]!=(i%3==0))return 2; }
{ B63 a={},b={}; for(int i=0;i<63;i++){a[i]=i%3==0;b[i]=i%2==0;}
B63 r=flip63(a,19,b); for(int i=0;i<63;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 26;
r=pressure63(1,2,3,4,5,6,7,8,a);for(int i=0;i<63;i++)if(r[i]!=(i%3==0))return 2; }
{ B64 a={},b={}; for(int i=0;i<64;i++){a[i]=i%3==0;b[i]=i%2==0;}
B64 r=flip64(a,19,b); for(int i=0;i<64;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 27;
r=pressure64(1,2,3,4,5,6,7,8,a);for(int i=0;i<64;i++)if(r[i]!=(i%3==0))return 2; }
{ B65 a={},b={}; for(int i=0;i<65;i++){a[i]=i%3==0;b[i]=i%2==0;}
B65 r=flip65(a,19,b); for(int i=0;i<65;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 28;
r=pressure65(1,2,3,4,5,6,7,8,a);for(int i=0;i<65;i++)if(r[i]!=(i%3==0))return 2; }
{ B127 a={},b={}; for(int i=0;i<127;i++){a[i]=i%3==0;b[i]=i%2==0;}
B127 r=flip127(a,19,b); for(int i=0;i<127;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 29;
r=pressure127(1,2,3,4,5,6,7,8,a);for(int i=0;i<127;i++)if(r[i]!=(i%3==0))return 2; }
{ B128 a={},b={}; for(int i=0;i<128;i++){a[i]=i%3==0;b[i]=i%2==0;}
B128 r=flip128(a,19,b); for(int i=0;i<128;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 30;
r=pressure128(1,2,3,4,5,6,7,8,a);for(int i=0;i<128;i++)if(r[i]!=(i%3==0))return 2; }
{ B129 a={},b={}; for(int i=0;i<129;i++){a[i]=i%3==0;b[i]=i%2==0;}
B129 r=flip129(a,19,b); for(int i=0;i<129;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 31;
r=pressure129(1,2,3,4,5,6,7,8,a);for(int i=0;i<129;i++)if(r[i]!=(i%3==0))return 2; }
{ B256 a={},b={}; for(int i=0;i<256;i++){a[i]=i%3==0;b[i]=i%2==0;}
B256 r=flip256(a,19,b); for(int i=0;i<256;i++)if(r[i]!=((i%3==0)^(i%2==0)))return 32;
r=pressure256(1,2,3,4,5,6,7,8,a);for(int i=0;i<256;i++)if(r[i]!=(i%3==0))return 2; }
return 0;
}
