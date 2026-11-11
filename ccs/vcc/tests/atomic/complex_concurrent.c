/* Compile with vpp -std=c2y -S, assemble/link with the target Linux C driver
 * and -pthread. Run natively on x86-64 or with qemu-aarch64 and its sysroot.
 * Four writers must not lose increments or change the imaginary sNaN bits. */
typedef unsigned long pthread_t;
extern int pthread_create(pthread_t *,const void *,void *(*)(void *),void *);
extern int pthread_join(pthread_t,void **);
static _Atomic(_Complex float) z;
static void *run(void *arg){for(int i=0;i<50000;i++)z++;return 0;}
int main(void){
 pthread_t t[4];z=__builtin_complex(0.0f,__builtin_nansf("0x123"));
 for(int i=0;i<4;i++)if(pthread_create(t+i,0,run,0))return 1;
 for(int i=0;i<4;i++)if(pthread_join(t[i],0))return 2;
 _Complex float r=z;union{float f;unsigned u;}im;im.f=__imag__ r;
 if(__real__ r!=200000.0f||im.u!=0x7f800123u)return 3;
 return 0;
}
