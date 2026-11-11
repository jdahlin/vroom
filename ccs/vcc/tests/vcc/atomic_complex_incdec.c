/* vcc-flags: -std=c2y -pedantic-errors */
static _Atomic(_Complex float) value;
static int calls;
static _Atomic(_Complex float) *once(void){calls++;return &value;}
int main(void){
 value=__builtin_complex(2.0f,-0.0f);
 _Complex float old=(*once())++;
 if(calls!=1||__real__ old!=2||!__builtin_signbit(__imag__ old))return 1;
 _Complex float now=--(*once());
 if(calls!=2||__real__ now!=2||!__builtin_signbit(__imag__ now))return 2;
 old=(*once())--;
 now=++(*once());
 if(calls!=4||__real__ old!=2||__real__ now!=2)return 3;
 value=__builtin_complex(1.0f,__builtin_nansf("0x123"));
 old=value++;now=value;
 union {float f;unsigned u;} a,b;
 a.f=__imag__ old;b.f=__imag__ now;
 if(a.u!=0x7f800123u||b.u!=a.u||__real__ now!=2)return 4;
 /* Keep the ordinary complex-float checks on every target. Half types are
  * optional: the Wasm reference does not implement _Float16 or __bf16. */
#ifdef __FLT16_MANT_DIG__
 typedef __typeof__(__builtin_complex((_Float16)1,(_Float16)2)) hc;
 _Atomic(hc) h=__builtin_complex((_Float16)1,(_Float16)-0.0);
 hc hr=++h;
 if(__real__ hr!=2||!__builtin_signbit(__imag__ hr))return 5;
#endif
 /* Clang has no __BFLT16_MANT_DIG__ capability macro on these targets.
  * Exercise bfloat on the two native targets that implement the type. */
#if defined(__aarch64__) || defined(__x86_64__)
 typedef __typeof__(__builtin_complex((__bf16)1,(__bf16)2)) bc;
 _Atomic(bc) bv=__builtin_complex((__bf16)1,(__bf16)-0.0);
 bc br=bv++;
 if(__real__ br!=1||!__builtin_signbit(__imag__ br))return 6;
#endif
 return 0;
}
