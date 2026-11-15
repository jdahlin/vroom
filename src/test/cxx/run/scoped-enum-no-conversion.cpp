// a scoped enumeration converts to no integer implicitly: f(int) is not viable for an enum class value (its
// user-defined conversion to W is the one), an unscoped one's is (doctest's has_insertion_operator<value_t>)
enum class vt : unsigned char { a, b };
enum ue { x1, x2 };
struct W { int k; W(vt v) : k((int)v) {} W(ue) : k(9) {} };
int f(int) { return 1; }
int f(W w) { return 10 + w.k; }
int main() { return f(vt::b) == 11 && f(x2) == 1 && f(5) == 1 ? 0 : 1; }
