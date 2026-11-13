/* Constant-specialized copies, ignored arguments, and nested once-only cost. */
#define NI __attribute__((noinline))
static unsigned long single_use(unsigned long x) {
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
static unsigned long wrapped(unsigned long x) {return single_use(x)+7;}
NI unsigned long layer1(unsigned long x) {return wrapped(x);}
NI unsigned long layer2(unsigned long x) {return wrapped(x)+3;}
static unsigned long specialize(unsigned long x,int mode) {
 (void)mode; /* This discard must not hide the real use below. */
 if(!mode)return x+5;
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
NI unsigned long constant0(unsigned long x) {return specialize(x,0);}
NI unsigned long constant1(unsigned long x) {return specialize(x,1);}
NI unsigned long general(unsigned long x,int mode) {return specialize(x,mode);}
static unsigned long unused(unsigned long x,int mode) {
 (void)mode;
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
NI unsigned long ignored0(unsigned long x) {return unused(x,0);}
NI unsigned long ignored1(unsigned long x) {return unused(x,1);}
int main(void) {
 for(unsigned long i=0;i<29;i++) {
  unsigned long x=i;
  for(unsigned j=91;j<115;j++)x=(x*33)^(x>>3)^j;
  if(layer1(i)!=x+7 || layer2(i)!=x+10)return 1;
  if(constant0(i)!=i+5 || constant1(i)!=x)return 2;
  if(general(i,0)!=i+5 || general(i,1)!=x)return 3;
  if(ignored0(i)!=x || ignored1(i)!=x)return 4;
 }
 return 0;
}
