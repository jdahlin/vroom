// r.w of a non-const local after a const member function was parsed: not const (the const member function's
// this_const applies to this->w alone); r.w.push_back(1), r.w = {...} take the non-const overloads
namespace std {
template <class E> class initializer_list {
	const E *__begin_; unsigned long __size_;
public:
	typedef E value_type;
	constexpr const E *begin() const { return __begin_; }
	constexpr unsigned long size() const { return __size_; }
};
}
struct V {
	int n = 0, last = 0;
	void push(int x) { n++, last = x; }
	int get() const { return last; }
	V &operator=(std::initializer_list<int> l) { n = (int)l.size(); last = l.size() ? l.begin()[l.size() - 1] : 0; return *this; }
};
struct R { V w; int peek() const { return w.get(); } };
int main()
{
	R r;
	r.w.push(4);
	int a = r.peek();
	r.w = {7, 8, 9};
	return a == 4 && r.w.n == 3 && r.w.last == 9 ? 0 : 1;
}
