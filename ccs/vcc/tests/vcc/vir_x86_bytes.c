/* Ordinary byte reads may combine, volatile reads and signed ones may not. */
#define NI __attribute__((noinline))
NI unsigned be16(const unsigned char *p) {return (p[0]<<8)|p[1];}
NI unsigned be32(const unsigned char *p) {return ((unsigned)p[0]<<24)|((unsigned)p[1]<<16)|(p[2]<<8)|p[3];}
NI unsigned indexed16(const unsigned char *p,long i) {
 const unsigned char *q=(const void *)((unsigned long)p+i);
 return (q[0]<<8)|q[1];
}
NI unsigned indexed32(const unsigned char *p,long i) {
 const unsigned char *q=(const void *)((unsigned long)p+i);
 return ((unsigned)q[0]<<24)|((unsigned)q[1]<<16)|(q[2]<<8)|q[3];
}
NI unsigned bev16(const volatile unsigned char *p) {return (p[0]<<8)|p[1];}
NI int signed16(const signed char *p) {return p[0]*256|p[1];}
NI unsigned changed(unsigned char *p) {unsigned hi=p[0]<<8;p[1]=17;return hi|p[1];}
int main(void) {
 unsigned char a[259];
 for(unsigned i=0;i<259;i++)a[i]=(unsigned char)(i*97+13);
 for(unsigned i=0;i<258;i++) {
  unsigned x=(unsigned)a[i]*256+a[i+1];
  if(be16(a+i)!=x||bev16(a+i)!=x)return 1;
  if(indexed16(a+128,(long)i-128)!=x)return 4;
  signed char *p=(signed char *)a+i;
  if(signed16(p)!=((int)p[0]*256|p[1]))return 2;
 }
 for(unsigned i=0;i<256;i++) {
  unsigned x=((unsigned)a[i]<<24)|((unsigned)a[i+1]<<16)|((unsigned)a[i+2]<<8)|a[i+3];
  if(be32(a+i)!=x||indexed32(a+128,(long)i-128)!=x)return 5;
 }
 for(unsigned i=0;i<258;i++) {
  unsigned x=(unsigned)a[i]*256+17;
  if(changed(a+i)!=x||a[i+1]!=17)return 3;
 }
 return 0;
}
