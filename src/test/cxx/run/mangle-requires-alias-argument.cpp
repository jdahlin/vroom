template<class A,class B> struct ct { typedef A type; };
template<class A,class B> using ctt = typename ct<A,B>::type;
template<class T> concept tw = sizeof(T) > 0;
template<class R> struct dur { R r; };
template <class R1, class R2>
  requires tw<ctt<R1, R2>>
constexpr auto cmp(const dur<R1>& a, const dur<R2>& b) { return a.r; }
int f(){ dur<int> a{1}, b{2}; return cmp(a,b); }
int main() { if (f() != 1) return 1; return 0; }
