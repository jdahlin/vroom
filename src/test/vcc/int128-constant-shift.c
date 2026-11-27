/* 128-bit shifts by constant counts and the high half of a 64x64 product,
 * results the same as clang's. */
int printf(const char *, ...);
typedef unsigned __int128 u128; typedef __int128 i128; typedef unsigned long u64;
#define F __attribute__((noinline)) static
F u64 hi(u64 a, u64 b) { return (u64)(((u128)a * b) >> 64); }
#define S(n) F void s##n(u128 v, u64 *o) { u128 a = v << n, b = v >> n; i128 c = (i128)v >> n; o[0] = (u64)a; o[1] = (u64)(a >> 64); o[2] = (u64)b; o[3] = (u64)(b >> 64); o[4] = (u64)c; o[5] = (u64)((u128)c >> 64); }
S(0) S(1) S(13) S(63) S(64) S(65) S(100) S(127)
int main(void) { u64 h = 0, o[6]; u64 x = 0x9e3779b97f4a7c15ul;
  for (int i = 0; i < 200; i++) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; u64 y = x * 31 + i; u128 v = (u128)y << 64 | x;
    h = h * 31 + hi(x, y);
#define R(n) s##n(v, o); for (int k = 0; k < 6; k++) h = h * 31 + o[k];
    R(0) R(1) R(13) R(63) R(64) R(65) R(100) R(127) }
  printf("%lx\n", h); }
