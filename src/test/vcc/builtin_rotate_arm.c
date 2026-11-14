/* __builtin_rotate*, __builtin_arm_{clz,rbit,cls}, barrier and hint builtins: result hash as clang -O1 */
#include <stdio.h>
#include <stdint.h>
__attribute__((noinline)) unsigned f8(uint8_t x,uint8_t n,int l){return l?__builtin_rotateleft8(x,n):__builtin_rotateright8(x,n);}
__attribute__((noinline)) unsigned f16(uint16_t x,uint16_t n,int l){return l?__builtin_rotateleft16(x,n):__builtin_rotateright16(x,n);}
__attribute__((noinline)) unsigned f32(uint32_t x,uint32_t n,int l){return l?__builtin_rotateleft32(x,n):__builtin_rotateright32(x,n);}
__attribute__((noinline)) uint64_t f64(uint64_t x,uint64_t n,int l){return l?__builtin_rotateleft64(x,n):__builtin_rotateright64(x,n);}
int main(void){uint64_t h=0;unsigned n[]={0,1,3,7,8,9,15,16,17,31,32,33,63,64,65,200};
for(int l=0;l<2;l++)for(int i=0;i<16;i++){h=h*31+f8(0xa5,n[i],l);h=h*31+f16(0xa51c,n[i],l);h=h*31+f32(0xa51c3e07u,n[i],l);h=h*31+f64(0xa51c3e0712345678ull,n[i],l);}
h=h*31+__builtin_rotateleft32(0x80000001u,4)+__builtin_arm_clz(0)+__builtin_arm_clz64(1)+__builtin_arm_rbit(1)+__builtin_arm_cls(0);
__builtin_arm_dmb(11);__builtin_arm_dsb(15);__builtin_arm_isb(15);__builtin_arm_nop();__builtin_arm_yield();__builtin_arm_sev();__builtin_arm_sevl();__builtin_arm_clrex();
printf("%llx\n",(unsigned long long)h);return h!=0xde124d3d3d8ba009ull;}
