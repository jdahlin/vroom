/* Low-half demand must follow loop parameters and selects, but stop when
 * either a full-width return or a full-width call argument reads the value. */
#define NI __attribute__((noinline))
NI int lowloop(int x,int n) {
 long long v=(long long)x;
 while(n-->0) {if(n&1)v=(long long)((int)v+1);else v=(long long)((int)v-1);}
 return (int)v;
}
NI long long wideloop(int x,int n) {
 long long v=(long long)x;
 while(n-->0) {if(n&1)v=(long long)((int)v+1);else v=(long long)((int)v-1);}
 return v;
}
NI int lowselect(int x,int choose) {long long v=x;return (int)(choose?v:-1LL);}
NI long long wideselect(int x,int choose) {long long v=x;return choose?v:-1LL;}
NI unsigned narrowcallee(unsigned x) {return x*17u+3u;}
NI unsigned narrowarg(int x) {long long v=x;return narrowcallee((unsigned)v);}
NI long long widecallee(long long x) {return x+0x100000000LL;}
NI long long widearg(int x) {long long v=x;return widecallee(v);}
int main(void) {
 for(int x=-32;x<=32;x++)for(int n=0;n<17;n++) {
  int expect=x-(n&1);
  if(lowloop(x,n)!=expect||wideloop(x,n)!=(long long)expect)return 1;
  if(lowselect(x,n)!=(n?x:-1)||wideselect(x,n)!=(n?(long long)x:-1LL))return 2;
  if(narrowarg(x)!=(unsigned)x*17u+3u||widearg(x)!=(long long)x+0x100000000LL)return 3;
 }
 return 0;
}
