/* CRC32C for host string hashes. x86 dispatches once at startup; the
 * software and SSE4.2 implementations use the same polynomial as ACLE. */
#ifndef VCC_PORT_CRC32C_H
#define VCC_PORT_CRC32C_H
#if defined(__x86_64__)
#include <stdint.h>
extern uint32_t (*vcrc32cd)(uint32_t, uint64_t);
extern uint32_t (*vcrc32cb)(uint32_t, uint8_t);
extern uint32_t (*vcrc32c_pair)(uint32_t, uint64_t, uint64_t);
#define __crc32cd(h, w) vcrc32cd((h), (w))
#define __crc32cb(h, b) vcrc32cb((h), (b))
#define vcrc32c_two(h, a, b) vcrc32c_pair((h), (a), (b))
#else
#include <arm_acle.h>
#define vcrc32c_two(h, a, b) __crc32cd(__crc32cd((h), (a)), (b))
#endif
#endif
