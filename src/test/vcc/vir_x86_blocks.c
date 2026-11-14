#define NI __attribute__((noinline))
#define COPY(N) NI void copy##N(void *d,const void *s) {__builtin_memcpy(d,s,N);}
#define ZERO(N) NI void zero##N(void *d) {__builtin_memset(d,0,N);}
COPY(0) COPY(1) COPY(2) COPY(3) COPY(7) COPY(8) COPY(15) COPY(16)
COPY(17) COPY(24) COPY(31) COPY(32) COPY(63) COPY(64) COPY(65) COPY(127) COPY(128) COPY(129)
ZERO(0) ZERO(1) ZERO(3) ZERO(7) ZERO(8) ZERO(15) ZERO(16) ZERO(17) ZERO(24) ZERO(255) ZERO(256) ZERO(257)
NI void dynamic(void *d,const void *s,unsigned long n) {__builtin_memcpy(d,s,n);}
NI void nonzero(void *d) {__builtin_memset(d,-37,24);}
NI void lowzero(void *d) {__builtin_memset(d,256,17);}
NI int readbyte(const unsigned char *p,unsigned i) {return p[i];}
NI int zero_local(unsigned i) {unsigned char data[256]={0};return readbyte(data,i);}
struct words { unsigned long lo, hi; };
NI unsigned long frame_words(struct words *out, unsigned long x) {
 struct words local = { x, ~x };
 *out = local;
 return local.hi;
}
NI int frame_test(unsigned char x) {
 unsigned char data[24] = {0};
 copy24(data, (unsigned char[24]){[9] = x});
 return (data[9] & 8) != 0;
}
int main(void) {
 struct words w;
 if(frame_words(&w,43) != ~43UL || w.lo != 43 || w.hi != ~43UL)return 7;
 if(frame_test(0) || !frame_test(8))return 8;
 unsigned char s[540],d[540];
 void (*copies[])(void *,const void *)={copy0,copy1,copy2,copy3,copy7,copy8,copy15,copy16,
  copy17,copy24,copy31,copy32,copy63,copy64,copy65,copy127,copy128,copy129};
 unsigned sizes[]={0,1,2,3,7,8,15,16,17,24,31,32,63,64,65,127,128,129};
 void (*zeros[])(void *)={zero0,zero1,zero3,zero7,zero8,zero15,zero16,zero17,zero24,zero255,zero256,zero257};
 unsigned zsizes[]={0,1,3,7,8,15,16,17,24,255,256,257};
 for(unsigned i=0;i<540;i++)s[i]=(unsigned char)(i*97+13);
 for(unsigned k=0;k<sizeof sizes/sizeof *sizes;k++) {
  for(unsigned i=0;i<540;i++)d[i]=201;
  copies[k](d+3,s+5);
  for(unsigned i=0;i<540;i++)if(d[i]!=(i>=3&&i<3+sizes[k]?s[i+2]:201))return 1;
 }
 for(unsigned k=0;k<sizeof zsizes/sizeof *zsizes;k++) {
  for(unsigned i=0;i<540;i++)d[i]=201;
  zeros[k](d+3);
  for(unsigned i=0;i<540;i++)if(d[i]!=(i>=3&&i<3+zsizes[k]?0:201))return 2;
 }
 for(unsigned i=0;i<540;i++)d[i]=201;
 dynamic(d+3,s+5,509);
 for(unsigned i=0;i<540;i++)if(d[i]!=(i>=3&&i<512?s[i+2]:201))return 3;
 nonzero(d+3);
 for(unsigned i=3;i<27;i++)if(d[i]!=219)return 4;
 lowzero(d+3);
 for(unsigned i=3;i<20;i++)if(d[i])return 5;
 for(unsigned i=0;i<256;i++)if(zero_local(i))return 6;
 return 0;
}
