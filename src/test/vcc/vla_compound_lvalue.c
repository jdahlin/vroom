/* Capturing a compound literal's VM type must preserve its lvalue category. */
__attribute__((noinline)) static int check(int n) {
  int a[8];
  int (**p)[2] = &(int (*)[++n]){(void *)a};
  if (n != 2 || *p != (void *)a) return 1;
  int i = 0;
  int (**q)[1] = &(typeof(++i, (int (*)[i])a)){(void *)a};
  if (i != 1 || *q != (void *)a) return 2;
  i = 0;
  (typeof(++i, (int (*)[i])a)){(void *)a} = 0;
  if (i != 1) return 3;
  i = 0;
  int (*r)[1] = (typeof(++i, (int (*)[i])a)){(void *)a} += 1;
  if (i != 1 || r != (void *)(a + 1)) return 4;
  i = 0;
  r = --(typeof(++i, (int (*)[i])a)){(void *)(a + 1)};
  if (i != 1 || r != (void *)a) return 5;
  i = 0;
  r = (typeof(++i, (int (*)[i])a)){(void *)a}++;
  if (i != 1 || r != (void *)a) return 6;
  return 0;
}
int main(void) {
  return check(1);
}
