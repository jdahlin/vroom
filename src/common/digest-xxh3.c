/*
 * XXH3-64 (seed 0, default secret), as lld uses it to shard SHF_MERGE pieces:
 * plain C, no dependency; the linker and the header cache key must reproduce
 * it bit for bit.
 */
#include "common/digest.h"
#include <string.h>

static inline uint64_t rd64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static inline uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint64_t rotl64(uint64_t x, int r) { return (x << r) | (x >> (64 - r)); }

/* ---- XXH3-64 ------------------------------------------------------------ */

#define P32_1 0x9E3779B1U
#define P32_2 0x85EBCA77U
#define P32_3 0xC2B2AE3DU
#define P64_1 0x9E3779B185EBCA87ULL
#define P64_2 0xC2B2AE3D27D4EB4FULL
#define P64_3 0x165667B19E3779F9ULL
#define P64_4 0x85EBCA77C2B2AE63ULL
#define P64_5 0x27D4EB2F165667C5ULL
#define PMX1 0x165667919E3779F9ULL
#define PMX2 0x9FB21C651E98DF25ULL

static const uint8_t kSecret[192] = {
    0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe, 0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c,
    0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb, 0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f,
    0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5, 0x78, 0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
    0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e, 0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c,
    0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb, 0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3,
    0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e, 0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
    0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f, 0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d,
    0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31, 0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64,
    0xea, 0xc5, 0xac, 0x83, 0x34, 0xd3, 0xeb, 0xc3, 0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb,
    0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49, 0xd3, 0x16, 0x55, 0x26, 0x29, 0xd4, 0x68, 0x9e,
    0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc, 0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce,
    0x45, 0xcb, 0x3a, 0x8f, 0x95, 0x16, 0x04, 0x28, 0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e,
};

static inline uint64_t fold64(uint64_t a, uint64_t b) {
    __uint128_t p = (__uint128_t)a * b;
    return (uint64_t)p ^ (uint64_t)(p >> 64);
}
static inline uint64_t xavalanche(uint64_t h) { h ^= h >> 37; h *= PMX1; return h ^ (h >> 32); }
static inline uint64_t x64avalanche(uint64_t h) {
    h ^= h >> 33; h *= P64_2; h ^= h >> 29; h *= P64_3; return h ^ (h >> 32);
}
static inline uint64_t rrmxmx(uint64_t h, uint64_t len) {
    h ^= rotl64(h, 49) ^ rotl64(h, 24); h *= PMX2; h ^= (h >> 35) + len; h *= PMX2;
    return h ^ (h >> 28);
}
static inline uint64_t mix16(const uint8_t *in, const uint8_t *s) {
    return fold64(rd64(in) ^ rd64(s), rd64(in + 8) ^ rd64(s + 8));
}
static inline void acc512(uint64_t *acc, const uint8_t *in, const uint8_t *s) {
    for (int i = 0; i < 8; i++) {
        uint64_t v = rd64(in + 8 * i), k = v ^ rd64(s + 8 * i);
        acc[i ^ 1] += v;
        acc[i] += (uint64_t)(uint32_t)k * (k >> 32);
    }
}

uint64_t vdigest_xxh3(const void *data, size_t len) {
    const uint8_t *in = data, *s = kSecret;
    if (len <= 16) {
        if (len > 8) {
            uint64_t lo = rd64(in) ^ (rd64(s + 24) ^ rd64(s + 32));
            uint64_t hi = rd64(in + len - 8) ^ (rd64(s + 40) ^ rd64(s + 48));
            return xavalanche(len + __builtin_bswap64(lo) + hi + fold64(lo, hi));
        }
        if (len >= 4) {
            uint64_t v = rd32(in + len - 4) + ((uint64_t)rd32(in) << 32);
            return rrmxmx(v ^ (rd64(s + 8) ^ rd64(s + 16)), len);
        }
        if (len) {
            uint32_t c = ((uint32_t)in[0] << 16) | ((uint32_t)in[len >> 1] << 24) | in[len - 1] |
                         ((uint32_t)len << 8);
            return x64avalanche(c ^ (uint64_t)(rd32(s) ^ rd32(s + 4)));
        }
        return x64avalanche(rd64(s + 56) ^ rd64(s + 64));
    }
    if (len <= 128) {
        uint64_t acc = len * P64_1;
        if (len > 32) {
            if (len > 64) {
                if (len > 96) { acc += mix16(in + 48, s + 96); acc += mix16(in + len - 64, s + 112); }
                acc += mix16(in + 32, s + 64); acc += mix16(in + len - 48, s + 80);
            }
            acc += mix16(in + 16, s + 32); acc += mix16(in + len - 32, s + 48);
        }
        acc += mix16(in, s); acc += mix16(in + len - 16, s + 16);
        return xavalanche(acc);
    }
    if (len <= 240) {
        uint64_t acc = len * P64_1;
        size_t n = len / 16;
        for (size_t i = 0; i < 8; i++) acc += mix16(in + 16 * i, s + 16 * i);
        acc = xavalanche(acc);
        for (size_t i = 8; i < n; i++) acc += mix16(in + 16 * i, s + 16 * (i - 8) + 3);
        acc += mix16(in + len - 16, s + 136 - 17);
        return xavalanche(acc);
    }
    uint64_t acc[8] = {P32_3, P64_1, P64_2, P64_3, P64_4, P32_2, P64_5, P32_1};
    const size_t nps = (192 - 64) / 8, blen = 64 * nps, nb = (len - 1) / blen;
    for (size_t b = 0; b < nb; b++) {
        for (size_t i = 0; i < nps; i++) acc512(acc, in + b * blen + i * 64, s + i * 8);
        for (int i = 0; i < 8; i++) {
            uint64_t a = acc[i];
            a ^= a >> 47; a ^= rd64(s + 128 + 8 * i); acc[i] = a * P32_1;
        }
    }
    size_t ns = ((len - 1) - blen * nb) / 64;
    for (size_t i = 0; i < ns; i++) acc512(acc, in + nb * blen + i * 64, s + i * 8);
    acc512(acc, in + len - 64, s + 192 - 64 - 7);
    uint64_t r = len * P64_1;
    for (int i = 0; i < 4; i++)
        r += fold64(acc[2 * i] ^ rd64(s + 11 + 16 * i), acc[2 * i + 1] ^ rd64(s + 11 + 16 * i + 8));
    return xavalanche(r);
}
