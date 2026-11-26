/* SSE2's pmulhw, pmaddwd and packs, which vcc expands into vector operations
   (frontend/c/builtins-vector.cpp), and SSSE3's pmaddubsw and SSE4.1's
   packusdw: extremes and pseudo-random operands, one checksum per builtin */
#include <stdio.h>
#if defined(__x86_64__)
typedef char v16qi __attribute__((vector_size(16)));
typedef short v8hi __attribute__((vector_size(16)));
typedef int v4si __attribute__((vector_size(16)));
typedef unsigned long long v2du __attribute__((vector_size(16)));

static unsigned long long hash[7];

static void add(int which, const void *p)
{
    const unsigned char *b = (const unsigned char *)p;
    for (int i = 0; i < 16; i++)
        hash[which] = (hash[which] ^ b[i]) * 0x100000001b3ull;
}

__attribute__((target("sse4.1"), noinline)) static void one(v2du a, v2du b)
{
    v8hi h = __builtin_ia32_pmulhw128((v8hi)a, (v8hi)b);
    add(0, &h);
    v4si d = __builtin_ia32_pmaddwd128((v8hi)a, (v8hi)b);
    add(1, &d);
    h = __builtin_ia32_pmaddubsw128((v16qi)a, (v16qi)b);
    add(2, &h);
    v16qi c = __builtin_ia32_packsswb128((v8hi)a, (v8hi)b);
    add(3, &c);
    h = __builtin_ia32_packssdw128((v4si)a, (v4si)b);
    add(4, &h);
    c = __builtin_ia32_packuswb128((v8hi)a, (v8hi)b);
    add(5, &c);
    h = (v8hi)__builtin_ia32_packusdw128((v4si)a, (v4si)b);
    add(6, &h);
}

int main(void)
{
    static const unsigned long long edge[] = {
        0, ~0ull, 0x8000800080008000ull, 0x7fff7fff7fff7fffull,
        0x8080808080808080ull, 0x7f7f7f7f7f7f7f7full, 0x0000800000007fffull,
        0xffff800000008000ull, 0x00ff0100ff7fff80ull, 0x000100000000ffffull};
    int n = sizeof edge / sizeof edge[0];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            v2du a = {edge[i], edge[(i + 1) % n]};
            v2du b = {edge[j], edge[(j + 3) % n]};
            one(a, b);
        }
    unsigned long long s = 1;
    for (int i = 0; i < 4000; i++) {
        unsigned long long r[4];
        for (int k = 0; k < 4; k++) {
            s = s * 6364136223846793005ull + 1442695040888963407ull;
            r[k] = s ^ (s >> 29);
            if (i & 1)
                r[k] &= 0x81ff81ff81ff81ffull;
        }
        v2du a = {r[0], r[1]}, b = {r[2], r[3]};
        one(a, b);
    }
    for (int i = 0; i < 7; i++)
        printf("%d %016llx\n", i, hash[i]);
    return 0;
}
#else
int main(void)
{
    return 0;
}
#endif
