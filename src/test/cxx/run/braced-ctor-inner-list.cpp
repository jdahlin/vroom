// T x{{...}} and T x = {{...}} by a constructor whose parameter is a class: the inner list a temporary of the
// parameter's class (json's byte_container_with_subtype<...> c{{0xCA, 0xFE}})
struct V { int n = 0, s = 0; V(int a, int b) : n(2), s(a + b) {} };
struct C { int n, s; C(const V &v) : n(v.n), s(v.s) {} };
int main() { C a{{3, 4}}; C b = {{5, 6}}; return a.n == 2 && a.s == 7 && b.s == 11 ? 0 : 1; }
