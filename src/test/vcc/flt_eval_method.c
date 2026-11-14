/* __FLT_EVAL_METHOD__ is one of clang's builtin macros (not in -dM): float.h
 * defines FLT_EVAL_METHOD as it, and so does MinGW-w64's float.h after it
 * (SQLite and zlib for Windows include it).  vcc did not have it: "use of
 * undeclared identifier".  0 on every target vcc has (x86-64 uses SSE). */
#include <float.h>

#if !defined(__FLT_EVAL_METHOD__) || __FLT_EVAL_METHOD__ != 0
#error __FLT_EVAL_METHOD__
#endif
#if FLT_EVAL_METHOD != 0
#error FLT_EVAL_METHOD
#endif

int main(void)
{
    int m = FLT_EVAL_METHOD, n = __FLT_EVAL_METHOD__;
    return m + n;
}
