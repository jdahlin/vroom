// a data member of a const object is const: its const member function overload; f(const T *) is more
// specialized than f(T *); p = v of a std::nullptr_t object
struct L { int *b; int *bp() { return b; } const int *bp() const { return b; } };
struct V { L l; };
template <class T> int g(T) { return sizeof(T); }
template <class T> int kind(T *) { return 1; }
template <class T> int kind(const T *) { return 2; }
int f(const V &x) { return kind(x.l.bp()); }

typedef decltype(nullptr) nt;
int *z(int *&p, nt &&v) { p = static_cast<nt &&>(v); return p; }
int test2() { int i, *p = &i; return z(p, nullptr) == nullptr ? 0 : 1; }
int main() { V v{}; return f(v) == 2 && test2() == 0 ? 0 : 1; }
