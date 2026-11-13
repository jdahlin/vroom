/* A VLA sizeof evaluates its operand, but does not access the array's data. */
__attribute__((noinline)) static int check(int n) {
  int count = 1;
  unsigned long size = sizeof (*(++count, (char (*)[count])0));
  if (count != 2 || size != 2) return 1;
  char a[3][n];
  char (*p)[n] = a;
  size = sizeof *p++;
  if (p != a + 1 || size != (unsigned long)n) return 2;
  size = sizeof *(++count, p);
  if (count != 3 || size != (unsigned long)n) return 3;
  size = sizeof *(++count, (char (*)[7])0);
  if (count != 3 || size != 7) return 4;
  size = sizeof *({ ++count; char (*q)[n] = p; q; });
  if (count != 4 || size != (unsigned long)n) return 5;
  int m = 2;
  size = sizeof(char[++m]);
  if (m != 3 || size != 3) return 6;
  char saved[++m];
  m = 9;
  if (sizeof saved != 4 || m != 9) return 7;
  return 0;
}
int main(void) {
  if (check(5) || check(11)) __builtin_abort();
  return 0;
}
