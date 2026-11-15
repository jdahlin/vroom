// extern template R f<args>(params); names the template's specialization, not a function f of its own that
// overload resolution would prefer (libc++'s extern template string operator+<char, ...>(char const*, string const&)); their definitions,
// explicit instantiations, at the end
template <class T> struct S { T v; };
template <class T> inline S<T> operator+(const T *l, const S<T> &r) { return S<T>{*l + r.v}; }
extern template S<int> operator+<int>(const int *, const S<int> &);
template <class T> inline T twice(T x) { return x + x; }
extern template int twice<int>(int);
int main() { int one = 1; S<int> s{2}; return (&one + s).v == 3 && twice(4) == 8 ? 0 : 1; }
template S<int> operator+<int>(const int *, const S<int> &);
template int twice<int>(int);
