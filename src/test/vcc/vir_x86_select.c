#define NI __attribute__((noinline))
NI long choose(long a,long b,long x,long y) {return a<b?x:y;}
NI unsigned long uchoose(unsigned long a,unsigned long b,unsigned long x,unsigned long y) {return a>b?x:y;}
NI long keep(long a,long b,long x,long y) {int c=a<b;return (c?x:y)+c;}
NI long boolean(int c,long x,long y) {return c?x:y;}
NI int *pointer(int c,int *x,int *y) {return c?x:y;}
int main(void) {
 long v[]={-10000000000L,-1,0,1,10000000000L};int x=1,y=2;
 for(int a=0;a<5;a++)for(int b=0;b<5;b++) {
  int c=v[a]<v[b];
  if(choose(v[a],v[b],37,-91)!=(c?37:-91))return 1;
  if(keep(v[a],v[b],37,-91)!=(c?38:-91))return 2;
  if(uchoose(v[a],v[b],~0UL,17)!=((unsigned long)v[a]>(unsigned long)v[b]?~0UL:17))return 3;
  if(boolean(v[a],v[a],v[b])!=(v[a]?v[a]:v[b]))return 4;
  if(pointer(c,&x,&y)!=(c?&x:&y))return 5;
 }
 return 0;
}
