// [dcl.init]/8: T() value-initializes; a class without constructors is
// zero-initialized first.
struct P {
  int x;
  int y;
};
struct Q {
  P p;
  long z;
};
int main() {
  P a = P();
  Q b = Q();
  P c = P(a);
  if (a.x != 0 || a.y != 0) return 1;
  if (b.p.x != 0 || b.p.y != 0 || b.z != 0) return 2;
  c.x = 5;
  P d = P(c);
  if (d.x != 5 || d.y != 0) return 3;
  return 0;
}
