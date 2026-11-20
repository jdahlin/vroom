// [dcl.fct]/1 ref-qualifiers of member functions, [over.match.funcs]/4-5: & takes an lvalue object,
// && an rvalue one, const & either; overloads are told apart by them;
// operators; out-of-class definitions; class templates; mangling (R, O)
struct S {
	int v;
	S(int x) : v(x) {}
	int get() & { return 1; }
	int get() && { return 2; }
	int get() const & { return 3; }
	int peek() const & { return 4; }
	int peek() &&;
	S *operator->() && { return this; }
	S &operator*() & { return *this; }
	S operator*() && { return S(v + 10); }
	int out() &;
	int out() const &&;
	int solo() & { return 8; }
	int duo() && { return 9; }
};
int S::peek() && { return 5; }
int S::out() & { return 6; }
int S::out() const && { return 7; }
template <class T> struct Box {
	T t;
	Box(T x) : t(x) {}
	T take() & { return t; }
	T take() && { return t + 1000; }
};
S make() { return S(1); }
const S cmake() { return S(1); }
int main() {
	S s(1);
	const S cs(1);
	int r = 0;
	if (s.get() != 1) r |= 1;
	if (make().get() != 2) r |= 2;
	if (cs.get() != 3) r |= 4;
	if (cmake().get() != 3) r |= 8; // const prvalue: const & (&& not const)
	if (static_cast<S &&>(s).get() != 2) r |= 16;
	if (s.peek() != 4 || make().peek() != 5 || cs.peek() != 4) r |= 32;
	if (s.out() != 6 || cmake().out() != 7) r |= 64;
	if (make()->v != 1) r |= 128;
	if ((*s).v != 1 || (*make()).v != 11) r |= 256;
	Box<int> b(5);
	if (b.take() != 5 || Box<int>(5).take() != 1005) r |= 512;
	int (S::*pl)() & = &S::solo;
	int (S::*pr)() && = &S::duo;
	if ((s.*pl)() != 8 || (make().*pr)() != 9) r |= 4096;
	return r;
}
