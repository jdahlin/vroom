/*
 * AddressSanitizer hooks for vcc's own allocators.  The bump regions live in
 * mmap'ed reservations that ASan does not track, so an ASan build poisons
 * what is not handed out yet and unpoisons each allocation as it goes out
 * (region.h).  Without ASan every macro is empty and VCC_ASAN is 0.
 *
 *   VASAN_POISON(p, n)     reads and writes of [p, p+n) are reported
 *   VASAN_UNPOISON(p, n)   ... are allowed again
 *   VASAN_WINDOW           how far ahead of the allocation point a region
 *                          keeps its tail poisoned: poisoning writes one
 *                          shadow byte per 8 bytes, so a 4 GB reservation is
 *                          poisoned a window at a time, not at once
 */
#ifndef VCC_COMMON_ASAN_H
#define VCC_COMMON_ASAN_H

#include <stddef.h>

#if defined(__SANITIZE_ADDRESS__)
#define VCC_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define VCC_ASAN 1
#endif
#endif
#ifndef VCC_ASAN
#define VCC_ASAN 0
#endif

#if VCC_ASAN
#include <sanitizer/asan_interface.h>
#define VASAN_POISON(p, n) ASAN_POISON_MEMORY_REGION((p), (n))
#define VASAN_UNPOISON(p, n) ASAN_UNPOISON_MEMORY_REGION((p), (n))
#define VASAN_WINDOW ((size_t)1 << 20)
#else
#define VASAN_POISON(p, n) ((void)0)
#define VASAN_UNPOISON(p, n) ((void)0)
#endif

#endif
