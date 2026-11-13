/* A static single-call helper larger than the direct walker inline limit. */
static unsigned long helper(unsigned long x) {
 x=(x*33)^(x>>3)^91UL;
 x=(x*33)^(x>>3)^92UL;
 x=(x*33)^(x>>3)^93UL;
 x=(x*33)^(x>>3)^94UL;
 x=(x*33)^(x>>3)^95UL;
 x=(x*33)^(x>>3)^96UL;
 x=(x*33)^(x>>3)^97UL;
 x=(x*33)^(x>>3)^98UL;
 x=(x*33)^(x>>3)^99UL;
 x=(x*33)^(x>>3)^100UL;
 x=(x*33)^(x>>3)^101UL;
 x=(x*33)^(x>>3)^102UL;
 x=(x*33)^(x>>3)^103UL;
 x=(x*33)^(x>>3)^104UL;
 x=(x*33)^(x>>3)^105UL;
 x=(x*33)^(x>>3)^106UL;
 x=(x*33)^(x>>3)^107UL;
 x=(x*33)^(x>>3)^108UL;
 x=(x*33)^(x>>3)^109UL;
 x=(x*33)^(x>>3)^110UL;
 x=(x*33)^(x>>3)^111UL;
 x=(x*33)^(x>>3)^112UL;
 x=(x*33)^(x>>3)^113UL;
 x=(x*33)^(x>>3)^114UL;
 return x;
}
__attribute__((noinline)) unsigned long work(unsigned long x) {return helper(x)+7;}
int main(void) {
 for(unsigned long i=0;i<19;i++) {
  unsigned long x=i;
  for(unsigned k=0;k<24;k++)x=(x*33)^(x>>3)^(k+91);
  if(work(i)!=x+7)return 1;
 }
 return 0;
}
