// long double values in generated code: arithmetic, conversions, a member, a parameter and a return
struct H { long double v; long double twice() const { return v * 2; } };
static long double add(long double a, double b) { return a + b; }
int main() {
  H h{1.25L};
  long double x = add(h.twice(), 0.5) / 3;	// 1.0
  int i = (int)(x * 10);
  return i == 10 && h.v < x + 1 ? 0 : 1;
}
