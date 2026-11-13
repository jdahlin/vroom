/* Default stays small enough for bare-metal QEMU. Compile-only checks also
   define FRAME_BYTES above 2 GiB to exercise the wide frame-offset path. */
#ifndef FRAME_BYTES
#define FRAME_BYTES 65536UL
#endif
static __attribute__((noinline)) int consume(const volatile unsigned char *p)
{
    return p[0] + p[FRAME_BYTES - 1];
}
static inline __attribute__((always_inline)) long double frame_raise(long double x) { return x + 1; }
static inline __attribute__((always_inline)) __int128 boost(__int128 x) { return x + 3; }
static __attribute__((noinline)) int plain(int n)
{
    volatile unsigned char bytes[FRAME_BYTES];
    bytes[0] = 17;
    bytes[FRAME_BYTES - 1] = 23;
    volatile long double value = 1.5L;
    volatile __int128 wide = ((__int128)1 << 80) + n;
    return consume(bytes) + (int)frame_raise(value) + (int)(boost(wide) & 255);
}
static __attribute__((noinline)) int aligned(int n)
{
    volatile unsigned char bytes[FRAME_BYTES] __attribute__((aligned(64)));
    bytes[0] = 9;
    bytes[FRAME_BYTES - 1] = 13;
    volatile long double value = 2.5L;
    volatile unsigned char vla[n];
    vla[0] = 3;
    vla[n - 1] = 5;
    int sum = consume(bytes);
#if defined(__x86_64__)
    __asm__("addl $1,%0" : "+r"(sum));
#else
    sum++;
#endif
    return sum + (int)value + vla[0] + vla[n - 1] + ((unsigned long)bytes % 64 != 0);
}
int main(void)
{
    return plain(7) != 52 || aligned(8) != 33;
}
