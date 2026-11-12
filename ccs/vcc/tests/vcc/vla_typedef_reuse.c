/* Capture each declaration execution, then reuse that value through typedefs. */
static int count;
static __attribute__((always_inline)) inline int inlined(int n) {
  typedef int A[(++count, n)];
  A *p = 0;
  A *q = 0;
  return sizeof *p + sizeof *q;
}
__attribute__((noinline)) static int check(int n) {
  typedef int A[++n];
  if (n != 4) return 1;
  n = 7;
  A a;
  A *p = &a;
  if (n != 7 || sizeof a != 4 * sizeof(int) || sizeof *p != sizeof a) return 2;
  int m = 2;
  typedef A B[++m];
  B b;
  if (m != 3 || n != 7 || sizeof b != 12 * sizeof(int)) return 3;
  int side = 0;
  typedef typeof(*(++side, &a)) C;
  C c;
  if (side != 1 || sizeof c != sizeof a) return 4;
  for (int i = 1; i != 20; ++i) {
    typedef int T[(++side, i)];
    T x;
    T y;
    if (side != i + 1 || sizeof x != i * sizeof(int) || sizeof y != sizeof x) return 5;
  }
  count = 0;
  int first = inlined(3);
  int second = inlined(5);
  if (count != 2 || first != 6 * sizeof(int) || second != 10 * sizeof(int)) return 6;
  return 0;
}
int main(void) {
  if (check(3)) __builtin_abort();
  return 0;
}
