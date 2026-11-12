/* VIR's dense/sparse switches, edge copies, atomic memory and wide multiply. */
#define NI __attribute__((noinline))
NI int dense(unsigned x,int a,int b) {
 int r;
 switch(x) {
 case 0:r=a+b;break;case 1:r=a-b;break;case 2:r=a*3+b;break;
 case 3:r=a^b;break;case 4:r=a*5-b;break;case 5:r=a+b*7;break;
 case 6:r=a-b*9;break;case 7:r=a*11+b;break;case 8:r=a|b;break;
 case 9:r=a&b;break;case 10:r=a*13+b;break;case 11:r=a+b*17;break;
 default:r=a+b+19;break;
 }
 return r*3+a;
}
NI int signed_range(long x,int a) {
 switch(x) {case -9 ... -5:return a+2;case 7 ... 11:return a-3;
 case 0x100000000L:return a*5;case (-0x7fffffffffffffffL-1):return a*7;default:return a^91;}
}
NI unsigned long high(unsigned long x) {return x/37;}
NI long shigh(long x) {return x/37;}
NI unsigned atom(unsigned *p,unsigned value) {
 __atomic_store_n(p,value,__ATOMIC_SEQ_CST);
 __atomic_thread_fence(__ATOMIC_SEQ_CST);
 return __atomic_load_n(p,__ATOMIC_ACQUIRE);
}
int main(void) {
 static const int coeff[12]={15,-3,27,0,21,69,-75,75,0,0,87,159};
 for(unsigned x=0;x<14;x++) {
  int a=2,b=3,r;
  if(x==3)r=(a^b)*3+a;else if(x==8)r=(a|b)*3+a;else if(x==9)r=(a&b)*3+a;
  else if(x<12)r=coeff[x]+a;else r=(a+b+19)*3+a;
  if(dense(x,a,b)!=r)return 1;
 }
 if(dense(~0u,2,3)!=74)return 2;
 for(long i=-15;i<16;i++)if(signed_range(i,17)!=(i>=-9&&i<=-5?19:i>=7&&i<=11?14:(17^91)))return 3;
 if(signed_range(0x100000000L,17)!=85||signed_range(-0x7fffffffffffffffL-1,17)!=119)return 4;
 unsigned long u=~0UL; if(high(u)!=u/37)return 5;
 if(shigh(-123456789012345L)!=-123456789012345L/37)return 6;
 unsigned v=0;if(atom(&v,0x98765432u)!=0x98765432u||v!=0x98765432u)return 7;
 return 0;
}
