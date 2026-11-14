template <class A, class B> struct same { static constexpr bool value = false; };
template <class A> struct same<A, A> { static constexpr bool value = true; };
namespace n {
struct P {
	friend bool operator==(const P &, const P &) { return true; }
	friend long operator-(const P &, const P &);
	P operator-(int) const;
	friend void swap(P &, P &) { }
	friend int hidden(const P &);
};
template <class I> struct W {
	I i;
	friend bool operator!=(const W &a, const W &b) { return a.i != b.i; }
	template <class J> friend auto operator-(const W &a, const W<J> &b) -> decltype(a.i - b.i) { return a.i - b.i; }
	template <class J> friend bool operator<(const W &a, const W<J> &b) { return a.i < b.i; }
	W operator-(long) const;
};
struct D : P { };
}
n::P p; n::W<int *> w; n::W<const int *> cw; n::D d;
static_assert(same<decltype(p == p), bool>::value, "1");
static_assert(same<decltype(p - p), long>::value, "2");
static_assert(same<decltype(p - 1), n::P>::value, "3");
static_assert(same<decltype(swap(p, p)), void>::value, "4");
static_assert(same<decltype(hidden(p)), int>::value, "5");
static_assert(same<decltype(w != w), bool>::value, "6");
static_assert(same<decltype(w - w), long>::value, "7");
static_assert(same<decltype(w - cw), long>::value, "8");
static_assert(same<decltype(w - 2L), n::W<int *>>::value, "9");
static_assert(same<decltype(w < cw), bool>::value, "10");
static_assert(same<decltype(d == d), bool>::value, "11");
struct NoOp { };
struct Out { explicit operator bool() const; };
Out &operator<<(Out &, int);
template <class...> using vt = void;
template <class S, class T, class = void> struct streamable { static constexpr bool value = false; };
template <class S, class T> struct streamable<S, T, vt<decltype(*(S *)0 << *(T *)0)>> { static constexpr bool value = true; };
static_assert(streamable<Out, int>::value, "12");
static_assert(!streamable<Out, NoOp>::value, "13");
