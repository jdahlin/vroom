/* typeof evaluates variably modified operands, preserving captured bounds. */
__attribute__((noinline)) static int check(int n) {
  int a[1][n];
  typeof((n++, a)) p = a;
  if (n != 4 || sizeof *p != 3 * sizeof(int)) return 1;
  int i = 0;
  char storage[1];
  int size = sizeof(typeof(*(++i, (char (*)[i])storage)));
  if (i != 1 || size != 1) return 2;

  int rows = 2, cols = 3;
  int matrix[++rows][++cols];
  typeof(*(i++, &matrix)) copy;
  if (i != 2 || rows != 3 || cols != 4) return 3;
  if (sizeof copy != sizeof matrix) return 4;
  rows = 8;
  cols = 9;
  typeof(*(i++, &copy)) again;
  if (i != 3 || sizeof again != 12 * sizeof(int)) return 5;
  /* Non-VM operands remain unevaluated. */
  typeof(i++) scalar = 0;
  if (i != 3 || scalar) return 6;
  return 0;
}
int main(void) {
  if (check(3)) __builtin_abort();
  return 0;
}
