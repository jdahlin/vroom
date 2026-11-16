// f(x) inside a template, ordinary lookup finding a template f: a non-template f in x's namespace, exact for
// every argument, is the better one (json's int_to_string(result, value) and alt_string's own int_to_string)
struct S { int v = 0; };
namespace lib {
template <class T> void fill(T &t, unsigned long n) { t.v = (int)n + 1000; }
template <class T> T make(unsigned long n) { T r; fill(r, n); return r; }
}
void fill(S &s, unsigned long n) { s.v = (int)n; }
int main() { return lib::make<S>(7).v == 7 ? 0 : 1; }
