/*
 * Hash functions the linker needs to reproduce lld bit for bit:
 *   - XXH3-64 (seed 0, default secret): lld shards SHF_MERGE pieces by it.
 *   - BLAKE3: lld's --build-id=sha1/md5 are BLAKE3 truncated to 20/16 bytes,
 *     computed over 1 MiB chunks and then over the concatenated chunk hashes.
 * Both are plain C; the build-id is parallel over chunks, so per-core speed
 * matters less than having no dependency.
 */
#include "ldhash.h"
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

uint64_t ld_xxh3(const void *data, size_t len) {
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

/* ---- BLAKE3 ------------------------------------------------------------- */

static const uint32_t B3IV[8] = {0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
                                 0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19};
enum { CHUNK_START = 1, CHUNK_END = 2, PARENT = 4, ROOT = 8 };

static inline uint32_t rotr32(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }
#define G(a, b, c, d, x, y)                                                                        \
    do {                                                                                           \
        v[a] += v[b] + (x); v[d] = rotr32(v[d] ^ v[a], 16); v[c] += v[d];                          \
        v[b] = rotr32(v[b] ^ v[c], 12); v[a] += v[b] + (y); v[d] = rotr32(v[d] ^ v[a], 8);         \
        v[c] += v[d]; v[b] = rotr32(v[b] ^ v[c], 7);                                               \
    } while (0)

static void b3compress(const uint32_t cv[8], const uint8_t block[64], uint64_t ctr, uint32_t blen,
                       uint32_t flags, uint32_t out[16]) {
    uint32_t m[16], v[16];
    for (int i = 0; i < 16; i++) m[i] = rd32(block + 4 * i);
    for (int i = 0; i < 8; i++) v[i] = cv[i];
    for (int i = 0; i < 4; i++) v[8 + i] = B3IV[i];
    v[12] = (uint32_t)ctr; v[13] = (uint32_t)(ctr >> 32); v[14] = blen; v[15] = flags;
    static const uint8_t perm[16] = {2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8};
    for (int r = 0; r < 7; r++) {
        G(0, 4, 8, 12, m[0], m[1]); G(1, 5, 9, 13, m[2], m[3]);
        G(2, 6, 10, 14, m[4], m[5]); G(3, 7, 11, 15, m[6], m[7]);
        G(0, 5, 10, 15, m[8], m[9]); G(1, 6, 11, 12, m[10], m[11]);
        G(2, 7, 8, 13, m[12], m[13]); G(3, 4, 9, 14, m[14], m[15]);
        if (r < 6) {
            uint32_t t[16];
            for (int i = 0; i < 16; i++) t[i] = m[perm[i]];
            memcpy(m, t, sizeof t);
        }
    }
    for (int i = 0; i < 8; i++) { out[i] = v[i] ^ v[i + 8]; out[i + 8] = v[i + 8] ^ cv[i]; }
}

/* Hashes one chunk (<= 1024 bytes); returns its CV, or with root set the root output. */
static void b3chunk(const uint8_t *in, size_t len, uint64_t ctr, int root, uint32_t out[16]) {
    uint32_t cv[8];
    memcpy(cv, B3IV, sizeof cv);
    size_t nblk = len ? (len + 63) / 64 : 1;
    for (size_t b = 0; b < nblk; b++) {
        uint8_t blk[64] = {0};
        size_t bl = len - b * 64 < 64 ? len - b * 64 : 64;
        memcpy(blk, in + b * 64, bl);
        uint32_t fl = (b == 0 ? CHUNK_START : 0) | (b == nblk - 1 ? CHUNK_END : 0);
        if (b == nblk - 1 && root) fl |= ROOT;
        b3compress(cv, blk, ctr, (uint32_t)bl, fl, out);
        memcpy(cv, out, sizeof cv);
    }
}

static void b3parent(const uint32_t l[8], const uint32_t r[8], int root, uint32_t out[16]) {
    uint8_t blk[64];
    memcpy(blk, l, 32);
    memcpy(blk + 32, r, 32);
    b3compress(B3IV, blk, 0, 64, PARENT | (root ? ROOT : 0), out);
}

void ld_blake3(const void *data, size_t len, uint8_t *out, size_t outlen) {
    const uint8_t *in = data;
    uint32_t stack[54][8], o[16];
    int sp = 0;
    size_t nch = len ? (len + 1023) / 1024 : 1;
    for (size_t c = 0; c + 1 < nch; c++) {
        b3chunk(in + c * 1024, 1024, c, 0, o);
        uint64_t total = c + 1;
        uint32_t cv[8];
        memcpy(cv, o, 32);
        while (!(total & 1)) {
            b3parent(stack[--sp], cv, 0, o);
            memcpy(cv, o, 32);
            total >>= 1;
        }
        memcpy(stack[sp++], cv, 32);
    }
    size_t last = len - (nch - 1) * 1024;
    b3chunk(in + (nch - 1) * 1024, last, nch - 1, sp == 0, o);
    while (sp > 0) {
        uint32_t cv[8];
        memcpy(cv, o, 32);
        sp--;
        b3parent(stack[sp], cv, sp == 0, o);
    }
    uint8_t full[32];
    memcpy(full, o, 32);
    memcpy(out, full, outlen > 32 ? 32 : outlen);
}
