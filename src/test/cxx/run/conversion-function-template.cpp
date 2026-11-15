// template <class T> operator T() const: instantiated for the class a T x = y, T x(y), T(y) converts to
// (json's implicit conversions: person p = j)
struct P { int a = 0; };
struct J { int v = 4; template <class T> operator T() const { T t; t.a = v; return t; } };
int f() { J j; P p = j; P q(j); return p.a + q.a + P(j).a; }
int main() { return f() == 12 ? 0 : 1; }
