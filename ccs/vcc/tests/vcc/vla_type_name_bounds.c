typedef __builtin_va_list va_list;
static int count;
__attribute__((noinline)) static int vararg(int expected, ...) {
  va_list ap;
  __builtin_va_start(ap, expected);
  void *p = __builtin_va_arg(ap, typeof(int (*)[++count]));
  __builtin_va_end(ap);
  return count != expected || p != 0;
}
__attribute__((noinline)) static int check(int n) {
  int storage[16];
  (void)(int (*)[++n])storage;
  if (n != 2) return 1;
  (void)(int (*)[++n]){(void *)storage};
  if (n != 3) return 2;
  __auto_type p = (++n, (int (*)[++n])storage);
  if (n != 5 || sizeof *p != 5 * sizeof(int)) return 3;
  n = 11;
  if (sizeof *p != 5 * sizeof(int)) return 4;
  typedef char A[++n];
  (void)(A *)storage;
  if (n != 12 || sizeof(A) != 12) return 5;
  /* The pointer's size does not depend on the bound: this operand is unevaluated. */
  (void)sizeof((int (*)[++n])storage);
  if (n != 12) return 6;
  count = 3;
  if (vararg(4, (void *)0)) return 7;
  return 0;
}
int main(void) {
  if (check(1)) __builtin_abort();
  return 0;
}
