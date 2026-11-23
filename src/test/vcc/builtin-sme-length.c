/* The bare-metal runner starts at EL1. Enable SME locally on CPUs that have it. */
#if defined(__aarch64__) && !defined(__cplusplus)
#include <arm_sme.h>

__attribute__((target("arch=armv9-a+sme"), noinline))
static int check(void)
{
    uint64_t bytes = svcntsb();
    if (bytes < 16 || bytes > 256 || bytes % 16)
        return 1;
    return svcntsd() != bytes / 8;
}

int main(void)
{
    uint64_t features, control;
    __asm__ volatile("mrs %0, id_aa64pfr1_el1" : "=r"(features));
    if (!(features & (15ULL << 24)))
        return 0;

    __asm__ volatile("mrs %0, cpacr_el1" : "=r"(control));
    uint64_t enabled = control | (3ULL << 24);
    __asm__ volatile("msr cpacr_el1, %0\n\tisb" :: "r"(enabled) : "memory");
    int result = check();
    __asm__ volatile("msr cpacr_el1, %0\n\tisb" :: "r"(control) : "memory");
    return result;
}
#else
int main(void) { return 0; }
#endif
