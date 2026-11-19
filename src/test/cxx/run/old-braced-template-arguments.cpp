// a braced list as a constructor's argument: T x{a, {...}} builds a temporary of the parameter's class (or an
// aggregate), and in template argument deduction {...} against T, T& or T&& is a non-deduced context: T from
// its default (P's U2 = T), or no deduction at all (Q's forwarding constructor: Q(const A&, const B&) then)
template <bool B, class T = int> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
struct V { int n; V(int x) : n(x) {} V(int x, int y) : n(x * 10 + y) {} };
struct W { int a, b; };
template <class T> struct P {
	int x; T w;
	template <class U1 = int, class U2 = T> P(U1 &&a, U2 &&b) : x(a), w(static_cast<U2 &&>(b)) {}
};
template <class A, class B> struct Q {
	A first; B second;
	template <bool D = true, class = typename enable_if<D>::type> Q(const A &a, const B &b) : first(a), second(b) {}
	template <class U1, class U2> Q(U1 &&a, U2 &&b) : first(a), second(b) {}
};
static int f1() { P<W> p{1, {3, 4}}; return p.w.b; }
static int f2() { Q<const int, W> q{1, {3, 4}}; return q.second.a; }
static int f3() { Q<const int, V> q{1, {1, 2}}; return q.second.n; }
static int f4() { Q<const int, V> q{1, 5}; return q.second.n; }
int main() { return f1() == 4 && f2() == 3 && f3() == 12 && f4() == 5 ? 0 : 1; }
