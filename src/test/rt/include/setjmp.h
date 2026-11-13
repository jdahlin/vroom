#ifndef _RT_SETJMP_H
#define _RT_SETJMP_H
typedef long jmp_buf[22];
typedef long sigjmp_buf[22];
int setjmp(jmp_buf);
int _setjmp(jmp_buf);
void longjmp(jmp_buf, int) __attribute__((noreturn));
void _longjmp(jmp_buf, int) __attribute__((noreturn));
#define sigsetjmp(b, s) setjmp(b)
#define siglongjmp longjmp
#endif
