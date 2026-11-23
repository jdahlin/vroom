// GNU statement expressions: the statements run, the value is the last expression (scalars; a class value is not built yet)
static int n;
struct D { int i; D(int j) : i(j) {} ~D() { ++n; } };
int f(int k) { return ({ int t = k * 2; t + 1; }); }
int g(int k) { return ({ D d(k); d.i + 1; }); }
void h(int k) { ({ n += k; }); }
struct P { int x; };
P mk(int k) { P r; r = ({ n += 0; P q; q.x = k; q; }); return r; }
int main() { int a = f(3), b = g(4); h(5); return a == 7 && b == 5 && n == 6 && mk(9).x == 9 ? 0 : 1; }
