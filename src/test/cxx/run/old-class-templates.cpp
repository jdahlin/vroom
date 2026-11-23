/* member functions of class template instances */
template <class T> struct Box {
	T v;
	void set(T x) { v = x; }
	T get() const { return v; }
	T twice() const { return get() + get(); }
	static int tag() { return 7; }
};
template <class A, class B> struct Pair2 {
	A a; B b;
	Pair2(A x, B y) : a(x), b(y) { }
	A first() const { return a; }
	B second() const { return b; }
	long sum() const { return (long)a + (long)b; }
};
template <class T> struct Stack {
	T items[8];
	int n;
	Stack() : n(0) { }
	void push(T x) { items[n] = x; n++; }
	T pop() { n--; return items[n]; }
	bool empty() const { return n == 0; }
	int size() const { return n; }
};
namespace lib { template <class T> struct Wrap { T x; T &ref() { return x; } const T &cref() const { return x; } }; }
int main() {
	Box<int> bi; bi.set(21); if (bi.twice() != 42) return 1;
	Box<long> bl; bl.set(1L << 40); if (bl.get() != (1L << 40)) return 2;
	Box<double> bd; bd.set(1.25); if (bd.twice() != 2.5) return 3;
	if (Box<char>::tag() != 7) return 4;
	Pair2<int, long> p(3, 4L); if (p.first() != 3 || p.second() != 4 || p.sum() != 7) return 5;
	Pair2<char, short> q('a', 2); if (q.sum() != 99) return 6;
	Stack<int> s; if (!s.empty()) return 7;
	s.push(1); s.push(2); s.push(3);
	if (s.size() != 3 || s.pop() != 3 || s.pop() != 2 || s.size() != 1) return 8;
	lib::Wrap<int> w; w.ref() = 5; if (w.cref() != 5) return 9;
	return 0;
}
