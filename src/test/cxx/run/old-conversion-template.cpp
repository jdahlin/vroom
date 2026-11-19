// a conversion function template (json's template <typename ValueType> operator ValueType() const): its
// instance for the target type, mangled as clang does (cvT_), chosen over a constructor of the target that
// would need a second user-defined conversion (B b = j: J's operator B, not operator int then B(int))
struct B { int v; B(int x) : v(x * 100) {} B(const B &o) : v(o.v + 1) {} };
struct J { int v; template <class T> operator T() const { return T(v); } };
static int f1(const J &j) { int n = j; return n; }
static long f2(const J &j) { long n = j; return n; }
static int f3(const J &j) { B b = j; return b.v; }
int main() { J j{4}; return f1(j) == 4 && f2(j) == 4 && f3(j) == 400 ? 0 : 1; }
