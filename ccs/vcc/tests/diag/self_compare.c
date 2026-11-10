/* -Wtautological-compare: self-comparison (variables, members, constant-index elements; not floats, *p, macros) */
struct S { int a; } s, *ps;
int f(int x, int *p, float fl, volatile int v, int arr[2]){
  int r = 0;
  r += x == x; r += x != x; r += x < x; r += x <= x; r += x > x; r += x >= x;
  r += s.a == s.a; r += ps->a == ps->a; r += (x) == x; r += fl == fl; r += v == v;
  r += p == p; r += arr[0] == arr[0]; r += *p == *p; r += (x + 1) == (x + 1);
#define X x
  r += X == x;
  return r;
}
