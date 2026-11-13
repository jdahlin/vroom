/* Synthetic CPU/OS fixtures: never execute optional host instructions. */
#include <assert.h>
#include "src/lib/x86native.h"
typedef struct fixture { unsigned max, submax, xcr, osxsave, xcalls, subcalls; } fixture;
static bool read_cpuid(void *arg, unsigned leaf, unsigned sub, unsigned *a,
                      unsigned *b, unsigned *c, unsigned *d)
{
 fixture *f=arg; *a=*b=*c=*d=0;
 switch(leaf) {
 case 0: *a=f->max; memcpy(b,"Auth",4);memcpy(d,"enti",4);memcpy(c,"cAMD",4);break;
 case 1:
  /* AMD family26 model0 (Zen5); legacy SSE plus AVX capabilities. */
  *a=(15u<<8)|(11u<<20);*d=(1u<<8)|(1u<<15)|(1u<<23)|(1u<<24)|(1u<<25)|(1u<<26);
  *c=(1u<<0)|(1u<<9)|(1u<<19)|(1u<<20)|(1u<<12)|(1u<<26)|(1u<<28)|(1u<<29)|(f->osxsave<<27);break;
 case 7:
  assert(f->max>=7);
  if(sub==0){*a=f->submax;*b=(1u<<5)|(1u<<16);*d=(1u<<22)|(1u<<24)|(1u<<25);}
  else { assert(sub==1 && f->submax>=1);f->subcalls++;*d=(1u<<21); }
  break;
 case 0x80000000: *a=0x80000001;break;
 case 0x80000001: *d=1u<<29;break;
 default: assert(!"queried absent CPUID leaf");
 }
 return false;
}
static bool read_xcr(void *arg,unsigned *a,unsigned *d)
{ fixture*f=arg;assert(f->osxsave);f->xcalls++;*a=f->xcr;*d=0;return false; }
int main(void)
{
 fixture f={7,0,0,0,0,0};x86_native_io io={&f,read_cpuid,read_xcr};
 x86_features bits,off;const char *cpu;
 assert(x86_native_detect(&io,&bits,&off,&cpu));assert(!strcmp(cpu,"znver5"));
 assert(!f.xcalls && !f.subcalls);assert(!x86_feature_has(bits,X86_FEATURE_AVX));
 assert(x86_feature_has(off,X86_FEATURE_AVX));
 f.osxsave=1;f.xcr=2;
 assert(x86_native_detect(&io,&bits,&off,&cpu));assert(!x86_feature_has(bits,X86_FEATURE_AVX));
 f.xcr=6;
 assert(x86_native_detect(&io,&bits,&off,&cpu));assert(x86_feature_has(bits,X86_FEATURE_AVX));
 assert(x86_feature_has(bits,X86_FEATURE_AVX2));assert(!x86_feature_has(bits,X86_FEATURE_AVX512F));
 assert(!x86_feature_has(bits,X86_FEATURE_AMX_TILE));
 f.xcr=0xe6;
 assert(x86_native_detect(&io,&bits,&off,&cpu));assert(x86_feature_has(bits,X86_FEATURE_AVX512F));
 assert(!x86_feature_has(bits,X86_FEATURE_AMX_TILE));
 f.xcr|=3u<<17;f.submax=1;
 assert(x86_native_detect(&io,&bits,&off,&cpu));assert(f.subcalls);
 assert(x86_feature_has(bits,X86_FEATURE_AMX_TILE));assert(!x86_feature_has(bits,X86_FEATURE_EGPR));
 f.xcr|=1u<<19;
 assert(x86_native_detect(&io,&bits,&off,&cpu));assert(x86_feature_has(bits,X86_FEATURE_EGPR));
 f.max=1;f.subcalls=0;
 assert(x86_native_detect(&io,&bits,&off,&cpu));assert(!f.subcalls);
 assert(!x86_feature_has(bits,X86_FEATURE_AVX2));
 f.max=0;assert(!x86_native_detect(&io,&bits,&off,&cpu));
 return 0;
}
