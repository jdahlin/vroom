/* SME streaming vector length queries. */
#ifndef __VCC_ARM_SME_H
#define __VCC_ARM_SME_H

#if defined(__aarch64__)
#include <stdint.h>

static __inline__ __attribute__((__always_inline__)) uint64_t svcntsb(void)
{
    uint64_t bytes;
    __asm__ volatile("rdsvl %0, #1" : "=r"(bytes));
    return bytes;
}

static __inline__ __attribute__((__always_inline__)) uint64_t svcntsd(void)
{
    return svcntsb() / 8;
}
#endif

#endif
