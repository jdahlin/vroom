// x = v for a union (or trivially copyable class) with converting constructors: the matching one's temporary, copied
union V { long i; double d; bool b; V() = default; V(long v) noexcept : i(v) {} V(double v) noexcept : d(v) {} V(bool v) noexcept : b(v) {} };
struct S { V v; };
int main() { S s; s.v = 5L; long a = s.v.i; s.v = 2.5; return a == 5 && s.v.d == 2.5 ? 0 : 1; }
