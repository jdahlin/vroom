// T({...}) with a constructor whose parameter is a class: the list a temporary of the parameter's class (json({1, 2}))
struct V { int a, b; };
struct W { int x; W(int a, int b) : x(a * 10 + b) {} };
struct J { int s; J(V v) : s(v.a + v.b) {} J(W w, int k) : s(w.x + k) {} };
bool eq(const J &a, const J &b) { return a.s == b.s; }
int main() { J j(V{1, 2}); return eq(j, J({1, 2})) && J({1, 2}, 5).s == 17 && J(V{3, 4}).s == 7 ? 0 : 1; }
