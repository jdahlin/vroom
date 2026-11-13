typedef __int128 T;
typedef unsigned __int128 U;
typedef T V __attribute__((vector_size(32)));
__attribute__((noinline)) T left(T x, T n) { return x << n; }
__attribute__((noinline)) T right(T x, T n) { return x >> n; }
__attribute__((noinline)) U logical(U x, U n) { return x >> n; }
__attribute__((noinline)) unsigned narrow(unsigned x, T n) { return x << n; }
__attribute__((noinline)) T lane(T n, V v) { v[1] |= v[1] << (n & 127); return v[1]; }
__attribute__((noinline)) T compound(T x, T n) { x <<= n; x >>= n; return x; }
__attribute__((noinline)) unsigned compound_narrow(unsigned x, T n) { x <<= n; return x; }
int main(void) {
  if (left(5, 1) != 10 || left(1, 100) != ((T)1 << 100)) __builtin_abort();
  if (right(-16, 2) != -4 || right(-1, 100) != -1) __builtin_abort();
  if (logical((U)1 << 100, 99) != 2 || logical((U)-1, 127) != 1) __builtin_abort();
  if (narrow(3, 4) != 48 || lane(1, (V){3, 5}) != 15) __builtin_abort();
  if (compound(5, 70) != 5 || compound_narrow(3, 4) != 48) __builtin_abort();
  return 0;
}
