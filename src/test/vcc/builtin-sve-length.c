#if defined(__aarch64__)
#include <arm_sve.h>

__attribute__((target("arch=armv8-a+sve"), noinline))
static int check(void)
{
    uint64_t bytes = svcntb();
    if (bytes < 16 || bytes > 256 || bytes % 16)
        return 1;
    if (svcnth() != bytes / 2 || svcntw() != bytes / 4 || svcntd() != bytes / 8)
        return 2;
    return 0;
}

int main(void) { return check(); }
#else
int main(void) { return 0; }
#endif
