/* Low-half demand must follow loop parameters and selects, but stop when
 * either a full-width return or a full-width call argument reads the value. */
#define NI __attribute__((noinline))
NI int lowloop(int x,int n) {
 long v=(long)x;
 while(n-->0) {if(n&1)v=(long)((int)v+1);else v=(long)((int)v-1);}
 return (int)v;
}
NI long wideloop(int x,int n) {
 long v=(long)x;
 while(n-->0) {if(n&1)v=(long)((int)v+1);else v=(long)((int)v-1);}
 return v;
}
NI int lowselect(int x,int choose) {long v=x;return (int)(choose?v:-1L);}
NI long wideselect(int x,int choose) {long v=x;return choose?v:-1L;}
NI unsigned narrowcallee(unsigned x) {return x*17u+3u;}
NI unsigned narrowarg(int x) {long v=x;return narrowcallee((unsigned)v);}
NI long widecallee(long x) {return x+0x100000000L;}
NI long widearg(int x) {long v=x;return widecallee(v);}
int main(void) {
 for(int x=-32;x<=32;x++)for(int n=0;n<17;n++) {
  int expect=x-(n&1);
  if(lowloop(x,n)!=expect||wideloop(x,n)!=(long)expect)return 1;
  if(lowselect(x,n)!=(n?x:-1)||wideselect(x,n)!=(n?(long)x:-1L))return 2;
  if(narrowarg(x)!=(unsigned)x*17u+3u||widearg(x)!=(long)x+0x100000000L)return 3;
 }
 return 0;
}
