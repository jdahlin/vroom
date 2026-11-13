/*
 * BLAKE3, for lld's --build-id=sha1/md5 (BLAKE3 truncated to 20/16 bytes,
 * computed over 1 MiB chunks and then over the concatenated chunk hashes):
 * four chunks per NEON lane set on aarch64, plain C elsewhere.
 */
#include "common/digest.h"
#include <string.h>

static inline uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

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

#ifdef __aarch64__
#include <arm_neon.h>
#include <stdlib.h>

/* Four BLAKE3 compressions side by side, one lane per input (as BLAKE3's
 * blake3_neon.c hash_many): each input is nblocks 64-byte blocks, lane i's
 * counter is ctr + i * inc.  Writes the four 32-byte chaining values. */
static const uint8_t B3SCHED[7][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8},
    {3, 4, 10, 12, 13, 2, 7, 14, 6, 5, 9, 0, 11, 15, 8, 1},
    {10, 7, 12, 9, 14, 3, 13, 15, 4, 0, 11, 2, 5, 8, 1, 6},
    {12, 13, 9, 11, 15, 10, 14, 8, 7, 2, 5, 3, 0, 1, 6, 4},
    {9, 14, 11, 5, 8, 12, 15, 1, 13, 3, 0, 10, 2, 6, 4, 7},
    {11, 15, 5, 0, 1, 9, 8, 6, 14, 10, 2, 12, 3, 4, 7, 13},
};
#define R16(x) vreinterpretq_u32_u16(vrev32q_u16(vreinterpretq_u16_u32(x)))
#define RN(x, n) vsriq_n_u32(vshlq_n_u32(x, 32 - (n)), x, n)
#define R8(x) vreinterpretq_u32_u8(vqtbl1q_u8(vreinterpretq_u8_u32(x), rot8))
#define G4(a, b, c, d, x, y)                                                                       \
    do {                                                                                           \
        v[a] = vaddq_u32(vaddq_u32(v[a], v[b]), x); v[d] = R16(veorq_u32(v[d], v[a]));             \
        v[c] = vaddq_u32(v[c], v[d]); v[b] = RN(veorq_u32(v[b], v[c]), 12);                        \
        v[a] = vaddq_u32(vaddq_u32(v[a], v[b]), y); v[d] = R8(veorq_u32(v[d], v[a]));           \
        v[c] = vaddq_u32(v[c], v[d]); v[b] = RN(veorq_u32(v[b], v[c]), 7);                         \
    } while (0)
#define ROUND4(r)                                                                                  \
    do {                                                                                           \
        const uint8_t *s = B3SCHED[r];                                                             \
        G4(0, 4, 8, 12, m[s[0]], m[s[1]]); G4(1, 5, 9, 13, m[s[2]], m[s[3]]);                      \
        G4(2, 6, 10, 14, m[s[4]], m[s[5]]); G4(3, 7, 11, 15, m[s[6]], m[s[7]]);                    \
        G4(0, 5, 10, 15, m[s[8]], m[s[9]]); G4(1, 6, 11, 12, m[s[10]], m[s[11]]);                  \
        G4(2, 7, 8, 13, m[s[12]], m[s[13]]); G4(3, 4, 9, 14, m[s[14]], m[s[15]]);                  \
    } while (0)

static inline void tr4(uint32x4_t *a, uint32x4_t *b, uint32x4_t *c, uint32x4_t *d) {
    uint32x4_t t0 = vtrn1q_u32(*a, *b), t1 = vtrn2q_u32(*a, *b);
    uint32x4_t t2 = vtrn1q_u32(*c, *d), t3 = vtrn2q_u32(*c, *d);
    *a = vreinterpretq_u32_u64(vtrn1q_u64(vreinterpretq_u64_u32(t0), vreinterpretq_u64_u32(t2)));
    *c = vreinterpretq_u32_u64(vtrn2q_u64(vreinterpretq_u64_u32(t0), vreinterpretq_u64_u32(t2)));
    *b = vreinterpretq_u32_u64(vtrn1q_u64(vreinterpretq_u64_u32(t1), vreinterpretq_u64_u32(t3)));
    *d = vreinterpretq_u32_u64(vtrn2q_u64(vreinterpretq_u64_u32(t1), vreinterpretq_u64_u32(t3)));
}

static void b3hash4(const uint8_t *const in[4], size_t nblocks, uint64_t ctr, int inc,
                    uint32_t flags, uint32_t fstart, uint32_t fend, uint8_t *out) {
    static const uint8_t r8[16] = {1, 2, 3, 0, 5, 6, 7, 4, 9, 10, 11, 8, 13, 14, 15, 12};
    const uint8x16_t rot8 = vld1q_u8(r8);
    uint32x4_t h[8], m[16], v[16];
    for (int i = 0; i < 8; i++) h[i] = vdupq_n_u32(B3IV[i]);
    uint32_t lo[4], hi[4];
    for (int i = 0; i < 4; i++) {
        uint64_t c = ctr + (inc ? (uint64_t)i : 0);
        lo[i] = (uint32_t)c; hi[i] = (uint32_t)(c >> 32);
    }
    uint32x4_t vlo = vld1q_u32(lo), vhi = vld1q_u32(hi), vlen = vdupq_n_u32(64);
    for (size_t b = 0; b < nblocks; b++) {
        for (int q = 0; q < 4; q++) {
            for (int i = 0; i < 4; i++)
                m[4 * q + i] = vreinterpretq_u32_u8(vld1q_u8(in[i] + 64 * b + 16 * q));
            tr4(&m[4 * q], &m[4 * q + 1], &m[4 * q + 2], &m[4 * q + 3]);
        }
        uint32_t f = flags | (b == 0 ? fstart : 0) | (b == nblocks - 1 ? fend : 0);
        for (int i = 0; i < 8; i++) v[i] = h[i];
        for (int i = 0; i < 4; i++) v[8 + i] = vdupq_n_u32(B3IV[i]);
        v[12] = vlo; v[13] = vhi; v[14] = vlen; v[15] = vdupq_n_u32(f);
        ROUND4(0); ROUND4(1); ROUND4(2); ROUND4(3); ROUND4(4); ROUND4(5); ROUND4(6);
        for (int i = 0; i < 8; i++) h[i] = veorq_u32(v[i], v[i + 8]);
    }
    tr4(&h[0], &h[1], &h[2], &h[3]);
    tr4(&h[4], &h[5], &h[6], &h[7]);
    for (int i = 0; i < 4; i++) {
        vst1q_u32((uint32_t *)(void *)(out + 32 * i), h[i]);
        vst1q_u32((uint32_t *)(void *)(out + 32 * i + 16), h[i + 4]);
    }
}

/* Chunk CVs four at a time, then the tree level by level (pairing adjacent
 * CVs with an odd one carried up gives BLAKE3's left-complete tree). */
void ld_blake3(const void *data, size_t len, uint8_t *out, size_t outlen) {
    const uint8_t *in = data;
    uint32_t o[16];
    size_t nch = len ? (len + 1023) / 1024 : 1;
    if (nch == 1) {
        b3chunk(in, len, 0, 1, o);
        memcpy(out, o, outlen > 32 ? 32 : outlen);
        return;
    }
    uint32_t sbuf[1024][8], (*cv)[8] = nch <= 1024 ? sbuf : malloc(nch * 32);
    size_t c = 0;
    for (; c + 4 < nch; c += 4) {
        const uint8_t *p[4] = {in + c * 1024, in + (c + 1) * 1024, in + (c + 2) * 1024,
                               in + (c + 3) * 1024};
        b3hash4(p, 16, c, 1, 0, CHUNK_START, CHUNK_END, (uint8_t *)cv[c]);
    }
    for (; c < nch; c++) {
        size_t l = c + 1 < nch ? 1024 : len - c * 1024;
        b3chunk(in + c * 1024, l, c, 0, o);
        memcpy(cv[c], o, 32);
    }
    size_t n = nch;
    while (n > 2) {
        size_t np = n / 2, j = 0;
        for (; j + 4 <= np; j += 4) {
            const uint8_t *p[4] = {(uint8_t *)cv[2 * j], (uint8_t *)cv[2 * j + 2],
                                   (uint8_t *)cv[2 * j + 4], (uint8_t *)cv[2 * j + 6]};
            b3hash4(p, 1, 0, 0, PARENT, 0, 0, (uint8_t *)cv[j]);
        }
        for (; j < np; j++) {
            b3parent(cv[2 * j], cv[2 * j + 1], 0, o);
            memcpy(cv[j], o, 32);
        }
        if (n & 1) memcpy(cv[np], cv[n - 1], 32);
        n = (n + 1) / 2;
    }
    b3parent(cv[0], cv[1], 1, o);
    if (cv != sbuf) free(cv);
    memcpy(out, o, outlen > 32 ? 32 : outlen);
}
#else
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
#endif
