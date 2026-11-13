/* Windows x64 (MinGW-w64, UCRT): jmp_buf is the CRT's _JUMP_BUFFER (256 bytes,
   16-aligned, xmm6-15 included), so rt/include's LP64 one does not fit.
   setjmp is UCRT's _setjmp with a null frame: longjmp restores the registers
   without unwinding through .pdata (bench/vcc-win64.py; the unwinding form
   gets tests of its own, doc/PLAN-win64.md point 4).  Found before
   rt/include through -I include-win64. */
#ifndef _RT_SETJMP_H
#define _RT_SETJMP_H
typedef struct __attribute__((aligned(16))) { unsigned long long Part[2]; } _RT_JBTYPE;
typedef _RT_JBTYPE jmp_buf[16];
typedef _RT_JBTYPE sigjmp_buf[16];
int __rt_setjmp(jmp_buf, void *) __asm__("_setjmp") __attribute__((returns_twice));
void longjmp(jmp_buf, int) __attribute__((noreturn));
#define setjmp(b) __rt_setjmp((b), 0)
#define _setjmp(b) __rt_setjmp((b), 0)
#define _longjmp longjmp
#define sigsetjmp(b, s) setjmp(b)
#define siglongjmp longjmp
#endif
