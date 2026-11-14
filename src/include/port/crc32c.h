/*
 * CRC32C of 8-byte words, the hash of the identifier table (common/intern.h).
 * On arm64 it is ACLE's __crc32cd; on x86-64 the same names call through
 * pointers chosen once at start-up (crc32c.c: a software loop or SSE4.2), with
 * the polynomial of ACLE, so both hosts give the same hash.  vcrc32c_two(h, a, b)
 * is two words in one step.
 */
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
