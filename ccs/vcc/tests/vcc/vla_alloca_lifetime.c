/* alloca storage must survive the end of an intervening VLA scope (GCC vla-24). */
__attribute__((noinline)) static int check(int n) {
  char *p;
  {
    char v[n];
    p = __builtin_alloca(8);
    p[0] = 1;
    p[7] = 7;
  }
  char *q = __builtin_alloca(64);
  __builtin_memset(q, 0, 64);
  return p[0] != 1 || p[7] != 7;
}
__attribute__((noinline)) static void touch(char *p, int n) {
  p[0] = 3;
  p[n - 1] = 5;
  __asm__ volatile ("" : : "r" (p) : "memory");
}
/* An older alloca must not prevent later VLA iterations from being reclaimed. */
__attribute__((noinline)) static int bounded(int n) {
  char *p = __builtin_alloca(8);
  p[0] = 9;
  for (int i = 0; i < 16384; ++i) {
    char v[n];
    touch(v, n);
    if (v[0] != 3 || v[n - 1] != 5) return 1;
  }
  return p[0] != 9;
}
int main(void) {
  if (check(48) || check(17) || bounded(1024)) __builtin_abort();
  return 0;
}
