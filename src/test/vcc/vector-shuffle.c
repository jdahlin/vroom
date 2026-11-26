/* __builtin_shufflevector of 16-byte vectors: unpacks, dword and word
   shuffles, rotations, even lanes and arbitrary index lists (x86-64's SSE2
   selection, arch/x64/codegen/simd.cpp), the lanes -1 leaves open masked out;
   and the 32-bit lane multiply */
#include <stdio.h>
#include <string.h>
typedef signed char v1 __attribute__((vector_size(16)));
typedef short v2 __attribute__((vector_size(16)));
typedef int v4 __attribute__((vector_size(16)));
typedef long long v8 __attribute__((vector_size(16)));
typedef unsigned u4 __attribute__((vector_size(16)));
static unsigned long long hash = 0xcbf29ce484222325ull;
static void add(const void *p, unsigned keep, int w)
{
    const unsigned char *b = (const unsigned char *)p;
    for (int i = 0; i < 16; i++)
        if (keep & (1u << (i / w)))
            hash = (hash ^ b[i]) * 0x100000001b3ull;
}
static __attribute__((noinline)) void s0(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s1(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s2(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s3(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s4(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 16, 0, 17, 1, 18, 2, 19, 3, 20, 4, 21, 5, 22, 6, 23, 7);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s5(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 24, 8, 25, 9, 26, 10, 27, 11, 28, 12, 29, 13, 30, 14, 31, 15);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s6(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 16, 16, 17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s7(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 24, 24, 25, 25, 26, 26, 27, 27, 28, 28, 29, 29, 30, 30, 31, 31);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s8(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s9(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 0);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s10(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s11(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 0, 1);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s12(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s13(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 0, 1, 2);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s14(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s15(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 0, 1, 2, 3);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s16(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s17(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 0, 1, 2, 3, 4);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s18(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s19(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 0, 1, 2, 3, 4, 5);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s20(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s21(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 23, 24, 25, 26, 27, 28, 29, 30, 31, 0, 1, 2, 3, 4, 5, 6);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s22(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s23(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 24, 25, 26, 27, 28, 29, 30, 31, 0, 1, 2, 3, 4, 5, 6, 7);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s24(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s25(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 25, 26, 27, 28, 29, 30, 31, 0, 1, 2, 3, 4, 5, 6, 7, 8);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s26(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s27(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 26, 27, 28, 29, 30, 31, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s28(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s29(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 27, 28, 29, 30, 31, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s30(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s31(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 28, 29, 30, 31, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s32(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s33(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 29, 30, 31, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s34(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s35(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 30, 31, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s36(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s37(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 31, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s38(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s39(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s40(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s41(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 8, 9, 10, 11, 4, 5, 6, 7, 12, 13, 14, 15, 0, 1, 2, 3);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s42(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 0, 1, 2, 3, 0, 1, 2, 3, 8, 9, 10, 11, 0, 1, 2, 3);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s43(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 4, 5, 6, 7, 0, 1, 2, 3, 0, 1, 2, 3, 12, 13, 14, 15);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s44(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s45(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 28, 29, 30, 31, 16, 17, 18, 19, 16, 17, 18, 19, 20, 21, 22, 23);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s46(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 16, 17, 18, 19, 28, 29, 30, 31, 16, 17, 18, 19, 20, 21, 22, 23);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s47(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s48(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 20, 21, 22, 23, 16, 17, 18, 19, 24, 25, 26, 27, 20, 21, 22, 23);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s49(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 5, 11, 22, 5, 3, 2, 12, 30, 26, 19, 28, 28, 22, 18, 14, 10);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s50(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 14, 4, 18, 30, 20, 27, 17, 3, 6, 31, 25, 9, 20, 8, 30, 25);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s51(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 1, 3, 19, 20, 21, 30, 28, 3, 4, 16, 29, 3, 2, 18, 27, 17);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s52(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 23, 21, 0, 28, 21, 9, 6, 30, 2, 12, 17, 7, 14, 24, 24, 30);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s53(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 4, 9, 27, 24, 16, 7, 26, 16, 25, 21, 23, 13, 8, 4, 10, 8);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s54(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 13, 13, -1, 30, 10, 15, 17, -1, 8, 25, 22, 19, 7, 31, 2, 28);
    add(&r, 65403u, 1);
}
static __attribute__((noinline)) void s55(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 24, 24, 24, 24, 5, 29, 24, 2, 11, 3, 12, 27, 9, 6, 20, 2);
    add(&r, 65535u, 1);
}
static __attribute__((noinline)) void s56(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 5, -1, 8, 5, 22, 0, 3, 12, 23, 8, 15, 21, 22, 29, 6, 6);
    add(&r, 65533u, 1);
}
static __attribute__((noinline)) void s57(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, -1);
    add(&r, 32767u, 1);
}
static __attribute__((noinline)) void s58(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, -1, 15);
    add(&r, 49151u, 1);
}
static __attribute__((noinline)) void s59(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, -1);
    add(&r, 32767u, 1);
}
static __attribute__((noinline)) void s60(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, -1);
    add(&r, 32767u, 1);
}
static __attribute__((noinline)) void s61(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 16, 0, 17, 1, 18, 2, 19, 3, 20, -1, 21, 5, 22, 6, 23, 7);
    add(&r, 65023u, 1);
}
static __attribute__((noinline)) void s62(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 24, 8, -1, 9, 26, 10, 27, 11, 28, 12, 29, 13, 30, 14, 31, 15);
    add(&r, 65531u, 1);
}
static __attribute__((noinline)) void s63(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 16, 16, 17, 17, -1, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 23);
    add(&r, 65519u, 1);
}
static __attribute__((noinline)) void s64(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 24, 24, 25, -1, 26, 26, 27, 27, 28, 28, 29, 29, 30, 30, 31, 31);
    add(&r, 65527u, 1);
}
static __attribute__((noinline)) void s65(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, -1, 12, 13, 14, 15, 16);
    add(&r, 64511u, 1);
}
static __attribute__((noinline)) void s66(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 17, 18, 19, 20, 21, 22, 23, 24, -1, 26, 27, 28, 29, 30, 31, 0);
    add(&r, 65279u, 1);
}
static __attribute__((noinline)) void s67(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, -1);
    add(&r, 32767u, 1);
}
static __attribute__((noinline)) void s68(const v1 *a, const v1 *b)
{
    v1 r = __builtin_shufflevector(*a, *b, 18, 19, 20, 21, 22, -1, 24, 25, 26, 27, 28, 29, 30, 31, 0, 1);
    add(&r, 65503u, 1);
}
static __attribute__((noinline)) void s69(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 0, 0, 1, 1, 2, 2, 3, 3);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s70(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 4, 4, 5, 5, 6, 6, 7, 7);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s71(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 0, 8, 1, 9, 2, 10, 3, 11);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s72(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 4, 12, 5, 13, 6, 14, 7, 15);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s73(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 8, 0, 9, 1, 10, 2, 11, 3);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s74(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 12, 4, 13, 5, 14, 6, 15, 7);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s75(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 8, 8, 9, 9, 10, 10, 11, 11);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s76(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 12, 12, 13, 13, 14, 14, 15, 15);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s77(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 1, 2, 3, 4, 5, 6, 7, 8);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s78(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 9, 10, 11, 12, 13, 14, 15, 0);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s79(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 2, 3, 4, 5, 6, 7, 8, 9);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s80(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 10, 11, 12, 13, 14, 15, 0, 1);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s81(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 3, 4, 5, 6, 7, 8, 9, 10);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s82(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 11, 12, 13, 14, 15, 0, 1, 2);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s83(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 4, 5, 6, 7, 8, 9, 10, 11);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s84(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 12, 13, 14, 15, 0, 1, 2, 3);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s85(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 5, 6, 7, 8, 9, 10, 11, 12);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s86(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 13, 14, 15, 0, 1, 2, 3, 4);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s87(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 6, 7, 8, 9, 10, 11, 12, 13);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s88(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 14, 15, 0, 1, 2, 3, 4, 5);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s89(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 7, 8, 9, 10, 11, 12, 13, 14);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s90(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 15, 0, 1, 2, 3, 4, 5, 6);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s91(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 0, 2, 4, 6, 8, 10, 12, 14);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s92(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 1, 0, 3, 2, 5, 4, 7, 6);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s93(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 7, 6, 5, 4, 3, 2, 1, 0);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s94(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 0, 1, 2, 3, 4, 5, 2, 3);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s95(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 0, 2, 0, 2, 4, 5, 6, 7);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s96(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 0, 1, 2, 3, 6, 5, 6, 5);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s97(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 4, 5, 2, 3, 2, 3, 2, 3);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s98(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 3, 1, 1, 3, 4, 5, 6, 7);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s99(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 0, 1, 2, 3, 6, 4, 4, 6);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s100(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 6, 7, 4, 5, 2, 3, 4, 5);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s101(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 3, 2, 2, 0, 4, 5, 6, 7);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s102(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 0, 1, 2, 3, 5, 4, 5, 7);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s103(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 2, 3, 4, 5, 2, 3, 6, 7);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s104(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 0, 3, 2, 0, 4, 5, 6, 7);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s105(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 0, 1, 2, 3, 4, 7, 5, 7);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s106(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 10, 11, 14, 15, 12, 13, 8, 9);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s107(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 11, 11, 11, 8, 12, 13, 14, 15);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s108(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 8, 9, 10, 11, 13, 13, 13, 12);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s109(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 10, 11, 14, 15, 10, 11, 14, 15);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s110(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 10, 9, 9, 8, 12, 13, 14, 15);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s111(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 8, 9, 10, 11, 12, 12, 13, 15);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s112(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 10, 11, 10, 11, 8, 9, 12, 13);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s113(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 9, 10, 9, 10, 12, 13, 14, 15);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s114(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 8, 9, 10, 11, 14, 15, 13, 12);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s115(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 12, 13, 14, 15, 14, 15, 10, 11);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s116(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 9, 8, 11, 9, 12, 13, 14, 15);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s117(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 8, 9, 10, 11, 12, 13, 13, 13);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s118(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 14, 2, 0, 9, 15, 15, 14, 2);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s119(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 0, 6, 5, 7, 0, 2, 15, 13);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s120(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, -1, 1, 13, 9, 15, 15, 5, 7);
    add(&r, 254u, 2);
}
static __attribute__((noinline)) void s121(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 13, 15, 14, 15, 6, 15, 7, 5);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s122(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 13, 3, 12, 2, 11, 13, 9, 1);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s123(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 6, 12, 1, 5, 8, 2, 3, 10);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s124(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 3, 7, 3, 13, 6, 2, 11, 14);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s125(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 4, 6, 4, 12, 15, 11, 9, 12);
    add(&r, 255u, 2);
}
static __attribute__((noinline)) void s126(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 0, 0, 1, -1, 2, 2, 3, 3);
    add(&r, 247u, 2);
}
static __attribute__((noinline)) void s127(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 4, 4, 5, 5, 6, -1, 7, 7);
    add(&r, 223u, 2);
}
static __attribute__((noinline)) void s128(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 0, 8, 1, 9, 2, -1, 3, 11);
    add(&r, 223u, 2);
}
static __attribute__((noinline)) void s129(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 4, -1, 5, 13, 6, 14, 7, 15);
    add(&r, 253u, 2);
}
static __attribute__((noinline)) void s130(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 8, 0, 9, 1, 10, -1, 11, 3);
    add(&r, 223u, 2);
}
static __attribute__((noinline)) void s131(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, -1, 4, 13, 5, 14, 6, 15, 7);
    add(&r, 254u, 2);
}
static __attribute__((noinline)) void s132(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 8, 8, 9, 9, 10, -1, 11, 11);
    add(&r, 223u, 2);
}
static __attribute__((noinline)) void s133(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 12, 12, 13, 13, 14, 14, 15, -1);
    add(&r, 127u, 2);
}
static __attribute__((noinline)) void s134(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 1, 2, 3, 4, 5, 6, 7, -1);
    add(&r, 127u, 2);
}
static __attribute__((noinline)) void s135(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, -1, 10, 11, 12, 13, 14, 15, 0);
    add(&r, 254u, 2);
}
static __attribute__((noinline)) void s136(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 2, 3, 4, 5, 6, 7, -1, 9);
    add(&r, 191u, 2);
}
static __attribute__((noinline)) void s137(const v2 *a, const v2 *b)
{
    v2 r = __builtin_shufflevector(*a, *b, 10, 11, 12, 13, 14, -1, 0, 1);
    add(&r, 223u, 2);
}
static __attribute__((noinline)) void s138(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 0, 0, 1, 1);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s139(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 2, 2, 3, 3);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s140(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 0, 4, 1, 5);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s141(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 2, 6, 3, 7);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s142(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 4, 0, 5, 1);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s143(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 6, 2, 7, 3);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s144(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 4, 4, 5, 5);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s145(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 6, 6, 7, 7);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s146(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 1, 2, 3, 4);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s147(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 5, 6, 7, 0);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s148(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 2, 3, 4, 5);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s149(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 6, 7, 0, 1);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s150(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 3, 4, 5, 6);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s151(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 7, 0, 1, 2);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s152(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 0, 2, 4, 6);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s153(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 1, 0, 3, 2);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s154(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 3, 2, 1, 0);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s155(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 2, 0, 0, 1);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s156(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 0, 0, 6, 6);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s157(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 0, 1, 2, 1);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s158(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 3, 2, 7, 5);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s159(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 3, 2, 0, 2);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s160(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 0, 1, 7, 4);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s161(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 2, 0, 0, 2);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s162(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 0, 1, 4, 6);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s163(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 4, 7, 4, 6);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s164(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 3, 2, 5, 4);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s165(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 5, 4, 5, 6);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s166(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 0, 1, 5, 6);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s167(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 6, 5, 6, 7);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s168(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 1, 2, 6, 4);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s169(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 6, 4, 4, 4);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s170(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 1, 3, 5, 7);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s171(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 0, 5, 6, 7);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s172(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 5, 7, 3, 2);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s173(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 2, 4, 2, 1);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s174(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 5, 4, -1, 1);
    add(&r, 11u, 4);
}
static __attribute__((noinline)) void s175(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, -1, 0, 3, 5);
    add(&r, 14u, 4);
}
static __attribute__((noinline)) void s176(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 1, -1, 0, 5);
    add(&r, 13u, 4);
}
static __attribute__((noinline)) void s177(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 7, 3, 2, 3);
    add(&r, 15u, 4);
}
static __attribute__((noinline)) void s178(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, -1, 6, 1, 1);
    add(&r, 14u, 4);
}
static __attribute__((noinline)) void s179(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 0, 0, -1, 1);
    add(&r, 11u, 4);
}
static __attribute__((noinline)) void s180(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 2, 2, 3, -1);
    add(&r, 7u, 4);
}
static __attribute__((noinline)) void s181(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, -1, 4, 1, 5);
    add(&r, 14u, 4);
}
static __attribute__((noinline)) void s182(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 2, 6, -1, 7);
    add(&r, 11u, 4);
}
static __attribute__((noinline)) void s183(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 4, 0, -1, 1);
    add(&r, 11u, 4);
}
static __attribute__((noinline)) void s184(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 6, 2, -1, 3);
    add(&r, 11u, 4);
}
static __attribute__((noinline)) void s185(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 4, 4, -1, 5);
    add(&r, 11u, 4);
}
static __attribute__((noinline)) void s186(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 6, -1, 7, 7);
    add(&r, 13u, 4);
}
static __attribute__((noinline)) void s187(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, -1, 2, 3, 4);
    add(&r, 14u, 4);
}
static __attribute__((noinline)) void s188(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 5, 6, -1, 0);
    add(&r, 11u, 4);
}
static __attribute__((noinline)) void s189(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 2, -1, 4, 5);
    add(&r, 13u, 4);
}
static __attribute__((noinline)) void s190(const v4 *a, const v4 *b)
{
    v4 r = __builtin_shufflevector(*a, *b, 6, 7, -1, 1);
    add(&r, 11u, 4);
}
static __attribute__((noinline)) void s191(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 0, 0);
    add(&r, 3u, 8);
}
static __attribute__((noinline)) void s192(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 1, 1);
    add(&r, 3u, 8);
}
static __attribute__((noinline)) void s193(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 0, 2);
    add(&r, 3u, 8);
}
static __attribute__((noinline)) void s194(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 1, 3);
    add(&r, 3u, 8);
}
static __attribute__((noinline)) void s195(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 2, 0);
    add(&r, 3u, 8);
}
static __attribute__((noinline)) void s196(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 3, 1);
    add(&r, 3u, 8);
}
static __attribute__((noinline)) void s197(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 2, 2);
    add(&r, 3u, 8);
}
static __attribute__((noinline)) void s198(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 3, 3);
    add(&r, 3u, 8);
}
static __attribute__((noinline)) void s199(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 1, 2);
    add(&r, 3u, 8);
}
static __attribute__((noinline)) void s200(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 3, 0);
    add(&r, 3u, 8);
}
static __attribute__((noinline)) void s201(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 0, 2);
    add(&r, 3u, 8);
}
static __attribute__((noinline)) void s202(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 1, 0);
    add(&r, 3u, 8);
}
static __attribute__((noinline)) void s203(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 1, 0);
    add(&r, 3u, 8);
}
static __attribute__((noinline)) void s204(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 0, -1);
    add(&r, 1u, 8);
}
static __attribute__((noinline)) void s205(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 1, 2);
    add(&r, 3u, 8);
}
static __attribute__((noinline)) void s206(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, -1, 2);
    add(&r, 2u, 8);
}
static __attribute__((noinline)) void s207(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 1, 3);
    add(&r, 3u, 8);
}
static __attribute__((noinline)) void s208(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 0, 0);
    add(&r, 3u, 8);
}
static __attribute__((noinline)) void s209(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 3, -1);
    add(&r, 1u, 8);
}
static __attribute__((noinline)) void s210(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, -1, 1);
    add(&r, 2u, 8);
}
static __attribute__((noinline)) void s211(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, -1, 0);
    add(&r, 2u, 8);
}
static __attribute__((noinline)) void s212(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 0, -1);
    add(&r, 1u, 8);
}
static __attribute__((noinline)) void s213(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, -1, 1);
    add(&r, 2u, 8);
}
static __attribute__((noinline)) void s214(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 0, -1);
    add(&r, 1u, 8);
}
static __attribute__((noinline)) void s215(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, -1, 3);
    add(&r, 2u, 8);
}
static __attribute__((noinline)) void s216(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 2, -1);
    add(&r, 1u, 8);
}
static __attribute__((noinline)) void s217(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 3, -1);
    add(&r, 1u, 8);
}
static __attribute__((noinline)) void s218(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, -1, 2);
    add(&r, 2u, 8);
}
static __attribute__((noinline)) void s219(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, -1, 3);
    add(&r, 2u, 8);
}
static __attribute__((noinline)) void s220(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, -1, 2);
    add(&r, 2u, 8);
}
static __attribute__((noinline)) void s221(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 3, -1);
    add(&r, 1u, 8);
}
static __attribute__((noinline)) void s222(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 0, -1);
    add(&r, 1u, 8);
}
static __attribute__((noinline)) void s223(const v8 *a, const v8 *b)
{
    v8 r = __builtin_shufflevector(*a, *b, 1, -1);
    add(&r, 1u, 8);
}
static __attribute__((noinline)) void multiply(const u4 *a, const u4 *b)
{
    u4 r = *a * *b;
    add(&r, 15u, 4);
}
int main(void)
{
    unsigned long long s = 1;
    for (int round = 0; round < 20; round++) {
        unsigned long long x[4];
        for (int k = 0; k < 4; k++) {
            s = s * 6364136223846793005ull + 1442695040888963407ull;
            x[k] = s ^ (s >> 29);
        }
        v1 a, b;
        memcpy(&a, x, 16), memcpy(&b, x + 2, 16);
        s0((const v1 *)&a, (const v1 *)&b);
        s1((const v1 *)&a, (const v1 *)&b);
        s2((const v1 *)&a, (const v1 *)&b);
        s3((const v1 *)&a, (const v1 *)&b);
        s4((const v1 *)&a, (const v1 *)&b);
        s5((const v1 *)&a, (const v1 *)&b);
        s6((const v1 *)&a, (const v1 *)&b);
        s7((const v1 *)&a, (const v1 *)&b);
        s8((const v1 *)&a, (const v1 *)&b);
        s9((const v1 *)&a, (const v1 *)&b);
        s10((const v1 *)&a, (const v1 *)&b);
        s11((const v1 *)&a, (const v1 *)&b);
        s12((const v1 *)&a, (const v1 *)&b);
        s13((const v1 *)&a, (const v1 *)&b);
        s14((const v1 *)&a, (const v1 *)&b);
        s15((const v1 *)&a, (const v1 *)&b);
        s16((const v1 *)&a, (const v1 *)&b);
        s17((const v1 *)&a, (const v1 *)&b);
        s18((const v1 *)&a, (const v1 *)&b);
        s19((const v1 *)&a, (const v1 *)&b);
        s20((const v1 *)&a, (const v1 *)&b);
        s21((const v1 *)&a, (const v1 *)&b);
        s22((const v1 *)&a, (const v1 *)&b);
        s23((const v1 *)&a, (const v1 *)&b);
        s24((const v1 *)&a, (const v1 *)&b);
        s25((const v1 *)&a, (const v1 *)&b);
        s26((const v1 *)&a, (const v1 *)&b);
        s27((const v1 *)&a, (const v1 *)&b);
        s28((const v1 *)&a, (const v1 *)&b);
        s29((const v1 *)&a, (const v1 *)&b);
        s30((const v1 *)&a, (const v1 *)&b);
        s31((const v1 *)&a, (const v1 *)&b);
        s32((const v1 *)&a, (const v1 *)&b);
        s33((const v1 *)&a, (const v1 *)&b);
        s34((const v1 *)&a, (const v1 *)&b);
        s35((const v1 *)&a, (const v1 *)&b);
        s36((const v1 *)&a, (const v1 *)&b);
        s37((const v1 *)&a, (const v1 *)&b);
        s38((const v1 *)&a, (const v1 *)&b);
        s39((const v1 *)&a, (const v1 *)&b);
        s40((const v1 *)&a, (const v1 *)&b);
        s41((const v1 *)&a, (const v1 *)&b);
        s42((const v1 *)&a, (const v1 *)&b);
        s43((const v1 *)&a, (const v1 *)&b);
        s44((const v1 *)&a, (const v1 *)&b);
        s45((const v1 *)&a, (const v1 *)&b);
        s46((const v1 *)&a, (const v1 *)&b);
        s47((const v1 *)&a, (const v1 *)&b);
        s48((const v1 *)&a, (const v1 *)&b);
        s49((const v1 *)&a, (const v1 *)&b);
        s50((const v1 *)&a, (const v1 *)&b);
        s51((const v1 *)&a, (const v1 *)&b);
        s52((const v1 *)&a, (const v1 *)&b);
        s53((const v1 *)&a, (const v1 *)&b);
        s54((const v1 *)&a, (const v1 *)&b);
        s55((const v1 *)&a, (const v1 *)&b);
        s56((const v1 *)&a, (const v1 *)&b);
        s57((const v1 *)&a, (const v1 *)&b);
        s58((const v1 *)&a, (const v1 *)&b);
        s59((const v1 *)&a, (const v1 *)&b);
        s60((const v1 *)&a, (const v1 *)&b);
        s61((const v1 *)&a, (const v1 *)&b);
        s62((const v1 *)&a, (const v1 *)&b);
        s63((const v1 *)&a, (const v1 *)&b);
        s64((const v1 *)&a, (const v1 *)&b);
        s65((const v1 *)&a, (const v1 *)&b);
        s66((const v1 *)&a, (const v1 *)&b);
        s67((const v1 *)&a, (const v1 *)&b);
        s68((const v1 *)&a, (const v1 *)&b);
        s69((const v2 *)&a, (const v2 *)&b);
        s70((const v2 *)&a, (const v2 *)&b);
        s71((const v2 *)&a, (const v2 *)&b);
        s72((const v2 *)&a, (const v2 *)&b);
        s73((const v2 *)&a, (const v2 *)&b);
        s74((const v2 *)&a, (const v2 *)&b);
        s75((const v2 *)&a, (const v2 *)&b);
        s76((const v2 *)&a, (const v2 *)&b);
        s77((const v2 *)&a, (const v2 *)&b);
        s78((const v2 *)&a, (const v2 *)&b);
        s79((const v2 *)&a, (const v2 *)&b);
        s80((const v2 *)&a, (const v2 *)&b);
        s81((const v2 *)&a, (const v2 *)&b);
        s82((const v2 *)&a, (const v2 *)&b);
        s83((const v2 *)&a, (const v2 *)&b);
        s84((const v2 *)&a, (const v2 *)&b);
        s85((const v2 *)&a, (const v2 *)&b);
        s86((const v2 *)&a, (const v2 *)&b);
        s87((const v2 *)&a, (const v2 *)&b);
        s88((const v2 *)&a, (const v2 *)&b);
        s89((const v2 *)&a, (const v2 *)&b);
        s90((const v2 *)&a, (const v2 *)&b);
        s91((const v2 *)&a, (const v2 *)&b);
        s92((const v2 *)&a, (const v2 *)&b);
        s93((const v2 *)&a, (const v2 *)&b);
        s94((const v2 *)&a, (const v2 *)&b);
        s95((const v2 *)&a, (const v2 *)&b);
        s96((const v2 *)&a, (const v2 *)&b);
        s97((const v2 *)&a, (const v2 *)&b);
        s98((const v2 *)&a, (const v2 *)&b);
        s99((const v2 *)&a, (const v2 *)&b);
        s100((const v2 *)&a, (const v2 *)&b);
        s101((const v2 *)&a, (const v2 *)&b);
        s102((const v2 *)&a, (const v2 *)&b);
        s103((const v2 *)&a, (const v2 *)&b);
        s104((const v2 *)&a, (const v2 *)&b);
        s105((const v2 *)&a, (const v2 *)&b);
        s106((const v2 *)&a, (const v2 *)&b);
        s107((const v2 *)&a, (const v2 *)&b);
        s108((const v2 *)&a, (const v2 *)&b);
        s109((const v2 *)&a, (const v2 *)&b);
        s110((const v2 *)&a, (const v2 *)&b);
        s111((const v2 *)&a, (const v2 *)&b);
        s112((const v2 *)&a, (const v2 *)&b);
        s113((const v2 *)&a, (const v2 *)&b);
        s114((const v2 *)&a, (const v2 *)&b);
        s115((const v2 *)&a, (const v2 *)&b);
        s116((const v2 *)&a, (const v2 *)&b);
        s117((const v2 *)&a, (const v2 *)&b);
        s118((const v2 *)&a, (const v2 *)&b);
        s119((const v2 *)&a, (const v2 *)&b);
        s120((const v2 *)&a, (const v2 *)&b);
        s121((const v2 *)&a, (const v2 *)&b);
        s122((const v2 *)&a, (const v2 *)&b);
        s123((const v2 *)&a, (const v2 *)&b);
        s124((const v2 *)&a, (const v2 *)&b);
        s125((const v2 *)&a, (const v2 *)&b);
        s126((const v2 *)&a, (const v2 *)&b);
        s127((const v2 *)&a, (const v2 *)&b);
        s128((const v2 *)&a, (const v2 *)&b);
        s129((const v2 *)&a, (const v2 *)&b);
        s130((const v2 *)&a, (const v2 *)&b);
        s131((const v2 *)&a, (const v2 *)&b);
        s132((const v2 *)&a, (const v2 *)&b);
        s133((const v2 *)&a, (const v2 *)&b);
        s134((const v2 *)&a, (const v2 *)&b);
        s135((const v2 *)&a, (const v2 *)&b);
        s136((const v2 *)&a, (const v2 *)&b);
        s137((const v2 *)&a, (const v2 *)&b);
        s138((const v4 *)&a, (const v4 *)&b);
        s139((const v4 *)&a, (const v4 *)&b);
        s140((const v4 *)&a, (const v4 *)&b);
        s141((const v4 *)&a, (const v4 *)&b);
        s142((const v4 *)&a, (const v4 *)&b);
        s143((const v4 *)&a, (const v4 *)&b);
        s144((const v4 *)&a, (const v4 *)&b);
        s145((const v4 *)&a, (const v4 *)&b);
        s146((const v4 *)&a, (const v4 *)&b);
        s147((const v4 *)&a, (const v4 *)&b);
        s148((const v4 *)&a, (const v4 *)&b);
        s149((const v4 *)&a, (const v4 *)&b);
        s150((const v4 *)&a, (const v4 *)&b);
        s151((const v4 *)&a, (const v4 *)&b);
        s152((const v4 *)&a, (const v4 *)&b);
        s153((const v4 *)&a, (const v4 *)&b);
        s154((const v4 *)&a, (const v4 *)&b);
        s155((const v4 *)&a, (const v4 *)&b);
        s156((const v4 *)&a, (const v4 *)&b);
        s157((const v4 *)&a, (const v4 *)&b);
        s158((const v4 *)&a, (const v4 *)&b);
        s159((const v4 *)&a, (const v4 *)&b);
        s160((const v4 *)&a, (const v4 *)&b);
        s161((const v4 *)&a, (const v4 *)&b);
        s162((const v4 *)&a, (const v4 *)&b);
        s163((const v4 *)&a, (const v4 *)&b);
        s164((const v4 *)&a, (const v4 *)&b);
        s165((const v4 *)&a, (const v4 *)&b);
        s166((const v4 *)&a, (const v4 *)&b);
        s167((const v4 *)&a, (const v4 *)&b);
        s168((const v4 *)&a, (const v4 *)&b);
        s169((const v4 *)&a, (const v4 *)&b);
        s170((const v4 *)&a, (const v4 *)&b);
        s171((const v4 *)&a, (const v4 *)&b);
        s172((const v4 *)&a, (const v4 *)&b);
        s173((const v4 *)&a, (const v4 *)&b);
        s174((const v4 *)&a, (const v4 *)&b);
        s175((const v4 *)&a, (const v4 *)&b);
        s176((const v4 *)&a, (const v4 *)&b);
        s177((const v4 *)&a, (const v4 *)&b);
        s178((const v4 *)&a, (const v4 *)&b);
        s179((const v4 *)&a, (const v4 *)&b);
        s180((const v4 *)&a, (const v4 *)&b);
        s181((const v4 *)&a, (const v4 *)&b);
        s182((const v4 *)&a, (const v4 *)&b);
        s183((const v4 *)&a, (const v4 *)&b);
        s184((const v4 *)&a, (const v4 *)&b);
        s185((const v4 *)&a, (const v4 *)&b);
        s186((const v4 *)&a, (const v4 *)&b);
        s187((const v4 *)&a, (const v4 *)&b);
        s188((const v4 *)&a, (const v4 *)&b);
        s189((const v4 *)&a, (const v4 *)&b);
        s190((const v4 *)&a, (const v4 *)&b);
        s191((const v8 *)&a, (const v8 *)&b);
        s192((const v8 *)&a, (const v8 *)&b);
        s193((const v8 *)&a, (const v8 *)&b);
        s194((const v8 *)&a, (const v8 *)&b);
        s195((const v8 *)&a, (const v8 *)&b);
        s196((const v8 *)&a, (const v8 *)&b);
        s197((const v8 *)&a, (const v8 *)&b);
        s198((const v8 *)&a, (const v8 *)&b);
        s199((const v8 *)&a, (const v8 *)&b);
        s200((const v8 *)&a, (const v8 *)&b);
        s201((const v8 *)&a, (const v8 *)&b);
        s202((const v8 *)&a, (const v8 *)&b);
        s203((const v8 *)&a, (const v8 *)&b);
        s204((const v8 *)&a, (const v8 *)&b);
        s205((const v8 *)&a, (const v8 *)&b);
        s206((const v8 *)&a, (const v8 *)&b);
        s207((const v8 *)&a, (const v8 *)&b);
        s208((const v8 *)&a, (const v8 *)&b);
        s209((const v8 *)&a, (const v8 *)&b);
        s210((const v8 *)&a, (const v8 *)&b);
        s211((const v8 *)&a, (const v8 *)&b);
        s212((const v8 *)&a, (const v8 *)&b);
        s213((const v8 *)&a, (const v8 *)&b);
        s214((const v8 *)&a, (const v8 *)&b);
        s215((const v8 *)&a, (const v8 *)&b);
        s216((const v8 *)&a, (const v8 *)&b);
        s217((const v8 *)&a, (const v8 *)&b);
        s218((const v8 *)&a, (const v8 *)&b);
        s219((const v8 *)&a, (const v8 *)&b);
        s220((const v8 *)&a, (const v8 *)&b);
        s221((const v8 *)&a, (const v8 *)&b);
        s222((const v8 *)&a, (const v8 *)&b);
        s223((const v8 *)&a, (const v8 *)&b);
        multiply((const u4 *)&a, (const u4 *)&b);
    }
    printf("%016llx\n", hash);
    return 0;
}
