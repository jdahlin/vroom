#include <arm_neon.h>
#include <stdio.h>
int main(void){
 uint8x16_t t,i,d; for(int k=0;k<16;k++){t[k]=100+k;i[k]=(k*7)%20;d[k]=k;}
 uint8x16_t r=vqtbx1q_u8(d,t,i); unsigned s=0; for(int k=0;k<16;k++) s=s*31+r[k];
 uint8x8x2_t p={{vget_low_u8(t),vget_high_u8(t)}};
 uint8x8_t r2=vtbx2_u8(vget_low_u8(d),p,vget_high_u8(i)); for(int k=0;k<8;k++) s=s*31+r2[k];
 int8x16_t v; for(int k=0;k<16;k++) v[k]=(k*37)%50-20;
 int8x8_t m=vpmin_s8(vget_low_s8(v),vget_high_s8(v)); for(int k=0;k<8;k++) s=s*31+(unsigned char)m[k];
 s=s*31+(unsigned char)vminvq_s8(v)+(unsigned char)vget_lane_s8(m,3);
 uint64x2_t c=vcombine_u64(vmov_n_u64(5),vmov_n_u64(9)); s=s*31+c[0]*3+c[1];
 printf("%u\n",s); }
