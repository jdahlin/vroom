/* vcc-flags: -fexceptions */
/* __attribute__((cleanup)) under -fexceptions: the cleanups run when an exception unwinds their
   scope, innermost first, through a function with no cleanups of its own, and not for a variable
   whose scope was not entered yet.  The exception is a forced unwind (_Unwind_ForcedUnwind, as
   pthread_cancel's), so the test needs no C++ run time; its stop function longjmps back to main at
   the end of the stack.  Only on x86-64 Linux (doc/PLAN-vir-eh.md E1; arm64 is E2): the arm64
   suite's bare-metal run time has no unwinder. */
#if defined(__x86_64__) && defined(__linux__)
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unwind.h>

static int order[16], n;
static jmp_buf back;
static struct _Unwind_Exception exc;

static void note(int *p) { order[n++] = *p; }

static _Unwind_Reason_Code stop(int version, _Unwind_Action actions,
                                _Unwind_Exception_Class cls,
                                struct _Unwind_Exception *x,
                                struct _Unwind_Context *ctx, void *arg)
{
    (void)version, (void)cls, (void)x, (void)ctx, (void)arg;
    if (actions & _UA_END_OF_STACK)
        longjmp(back, 1);
    return _URC_NO_REASON;
}

__attribute__((noinline)) static void raise_it(void)
{
    memset(&exc, 0, sizeof exc);
    exc.exception_class = 0x7663630043000000ull; /* "vcc\0C\0\0\0" */
    _Unwind_ForcedUnwind(&exc, stop, 0);
    abort();
}

__attribute__((noinline)) static void plain(void) { raise_it(); }

__attribute__((noinline)) static int inner(int k)
{
    int a __attribute__((cleanup(note))) = 1;
    {
        int b __attribute__((cleanup(note))) = 2;
        if (k)
            plain();
    }
    int c __attribute__((cleanup(note))) = 3; /* not in scope at the throw */
    return a + c;
}

__attribute__((noinline)) static void outer(void)
{
    int d __attribute__((cleanup(note))) = 4;
    inner(1);
}

int main(void)
{
    if (inner(0) != 4 || n != 3 || order[0] != 2 || order[1] != 3 ||
        order[2] != 1)
        return 1;
    n = 0;
    if (!setjmp(back))
        outer();
    if (n != 3 || order[0] != 2 || order[1] != 1 || order[2] != 4)
        return 2;
    return 0;
}
#else
int main(void) { return 0; }
#endif
