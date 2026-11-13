/* Extend a no-overflow signed index before scaling, never a wrapping product. */
#define NI __attribute__((noinline))
#define BE16(p) (((unsigned)(p)[0]<<8)|(p)[1])
#define BE32(p) (((unsigned)(p)[0]<<24)|((unsigned)(p)[1]<<16)|((unsigned)(p)[2]<<8)|(p)[3])
NI unsigned be16p(const unsigned char *p,int i) {p+=i*2;return BE16(p);}
NI unsigned be16cast(unsigned long base,int i) {const unsigned char *p=(const void *)(base+(long)(i*2));return BE16(p);}
NI unsigned be32cast(unsigned long base,long i) {const unsigned char *p=(const void *)(base+i*4);return BE32(p);}
NI unsigned long long be64cast(unsigned long base,long i) {
 const unsigned char *p=(const void *)(base+i*8);
 unsigned long long hi=BE32(p);return (hi<<32)|BE32(p+4);
}
NI int load4signed(const int *p,int i) {return *(const int *)((unsigned long)p+(long)(i*4));}
NI void store4signed(volatile int *p,int i,int x) {*(volatile int *)((unsigned long)p+(long)(i*4))=x;}
NI unsigned shared(const unsigned char *p,int i,int *index) {int j=i*2;*index=j;p+=j;return BE16(p);}
NI unsigned keep_input(const unsigned char *p,int i) {p+=i*2;return BE16(p)+(unsigned)i;}
NI unsigned rawshift(unsigned long base,int i) {const unsigned char *p=(const void *)(base+(long)(i<<1));return BE16(p);}
NI unsigned vol(const volatile unsigned char *p,int i) {p+=i*2;return BE16(p);}
NI long min32(int x) {return (long)(x*(-2147483647-1));}
NI long long min64(long long x) {return x*(-9223372036854775807LL-1);}
NI unsigned clean16(const unsigned char *p,const unsigned short *i) {return p[2*(int)*i];}
NI unsigned clean8(const unsigned char *p,const signed char *i) {return p[2*(int)*i];}
NI int change(signed char *i,int x) {i[0]=(signed char)((unsigned char)i[0]^1);return x+7;}
NI unsigned pressure_indices(const unsigned char *p,signed char *i,int x) {
 int a=i[0],b=i[1],c=i[2],d=i[3],e=i[4],f=i[5],g=i[6];
 int h=i[7],j=i[8],k=i[9],l=i[10],m=i[11],n=i[12];
 unsigned r=change(i,x);
 return r+p[2*a]+p[2*b]+p[2*c]+p[2*d]+p[2*e]+p[2*f]+p[2*g]+
  p[2*h]+p[2*j]+p[2*k]+p[2*l]+p[2*m]+p[2*n];
}
int main(void) {
 static unsigned char table[131073];
 for(unsigned j=0;j<sizeof table;j++)table[j]=(unsigned char)((j*7)^(j>>3));
 for(unsigned j=0;j<65536;j+=17) {
  unsigned short i=j;if(clean16(table,&i)!=table[2*j])return 14;
 }
 for(int j=-128;j<128;j++) {
  signed char i=j;if(clean8(table+512,&i)!=table[512+2*j])return 15;
  signed char indices[13];unsigned expected=10;
  for(unsigned k=0;k<13;k++){indices[k]=(signed char)(j+k*37);expected+=table[512+2*(int)indices[k]];}
  if(pressure_indices(table+512,indices,3)!=expected)return 16;
 }
 unsigned char bytes[257];
 for(unsigned j=0;j<257;j++)bytes[j]=(unsigned char)(j*97+13);
 const unsigned char *mid=bytes+128;
 for(int i=-8;i<=8;i++) {
  const unsigned char *p=mid+i*2;unsigned x=(unsigned)p[0]*256+p[1];int index;
  if(be16p(mid,i)!=x || be16cast((unsigned long)mid,i)!=x || vol(mid,i)!=x)return 1;
  if(shared(mid,i,&index)!=x || index!=i*2 || keep_input(mid,i)!=x+(unsigned)i)return 2;
  p=mid+i*4;x=0;for(unsigned j=0;j<4;j++)x=x*256+p[j];
  if(be32cast((unsigned long)mid,i)!=x)return 3;
  p=mid+i*8;unsigned long long y=0;for(unsigned j=0;j<8;j++)y=y*256+p[j];
  if(be64cast((unsigned long)mid,i)!=y)return 4;
  if(i>=0 && rawshift((unsigned long)mid,i)!=BE16(mid+i*2))return 5;
 }
 int a[9];for(int i=-4;i<=4;i++) {store4signed(a+4,i,-100-i);if(load4signed(a+4,i)!=-100-i)return 6;}
 int indices[]={-1073741824,1073741823,-123456,123456,-1,0,1,
#ifdef WRAP_MODE
 (-2147483647-1),2147483647,1073741824,-1073741825,
#endif
 };
 for(unsigned j=0;j<sizeof indices/sizeof *indices;j++) {
  int i=indices[j];long offset=(int)((unsigned)i*2u);
  unsigned long base=(unsigned long)mid-(unsigned long)offset;
  if(be16cast(base,i)!=BE16(mid))return 7;
 }
 if(min32(0) || min32(1)!=(-2147483647L-1) || min64(0) || min64(1)!=(-9223372036854775807LL-1))return 8;
#ifdef WRAP_MODE
 if(min32(-1)!=(-2147483647L-1) || min32(2) || min64(-1)!=(-9223372036854775807LL-1) || min64(2))return 9;
#endif
 return 0;
}
