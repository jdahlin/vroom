/* [dcl.init]/17.6.1: a default member initializer that is a call initializes the member */
struct S { int v; S(int a) : v(a) {} S(const S &o) : v(o.v + 100) {} ~S() {} };
S make(int a) { return S(a); }
struct H { const S s = make(4); };
int main() { H h; return h.s.v != 4; }
