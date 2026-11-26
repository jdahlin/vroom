#if defined(__x86_64__)
/* functions clang's x86 headers only declare, and __rdtsc, which none
   declares: each call is the instruction, no symbol to link against */
#include <x86intrin.h>
#include <stdio.h>

static char line[64];
static volatile int flag;
static unsigned evaluations;

static char *target(int i)
{
    ++evaluations;
    return line + i;
}

static unsigned rounding(unsigned csr)
{
    ++evaluations;
    return csr;
}

int main(void)
{
    unsigned saved, aux = 0;
    unsigned long long first, last;

    first = __rdtsc();
    line[3] = 42;
    _mm_clflush(target(3));
    _mm_sfence();
    _mm_lfence();
    _mm_mfence();
    (_mm_pause)();
    _m_prefetch(target(1));
    _m_prefetchw(target(2));
    for (flag = 0; flag < 3; flag++)
        _mm_pause();
    saved = _mm_getcsr();
    _mm_setcsr(rounding(saved | 0x6000));
    printf("%x\n", _mm_getcsr() & 0x6000);
    _mm_setcsr(saved);
    printf("%d\n", _mm_getcsr() == saved);
    last = _rdtsc();
    printf("%d %d %u %d\n", line[3], flag, evaluations, last >= first);
    printf("%d\n", __rdtscp(&aux) != 0);
    return 0;
}
#else
int main(void)
{
    return 0;
}
#endif
