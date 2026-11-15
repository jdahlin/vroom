// an explicit instantiation of a function template declares no non-template function: the calls
// still bind to the template (before, to an undefined plain n::add, mangled without template arguments)
namespace n {
template <class T> struct S { T v; };
template <class T> S<T> add(const T *p, const S<T> &s) { return S<T>{*p + s.v}; }
template S<int> add<int>(const int *, const S<int> &);
template <class T> T twice(T x) { return x + x; }
template int twice(int);
template long twice(long);
}
int main() { int one = 1; n::S<int> s{2}; return n::add(&one, s).v + n::twice(3) == 9 ? 0 : 1; }
