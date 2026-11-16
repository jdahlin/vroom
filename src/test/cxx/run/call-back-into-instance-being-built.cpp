// guessed-names: 1
// json(const ordered_json &): the converting constructor template's body constructs its elements through a
// helper (construct_at) that calls the same constructor again, while that body is still being built (the
// constructor's name is a guess of vcc's: the call is allowed since the body under way defines it)
typedef unsigned long size_t;
void *operator new(size_t, void *p) noexcept { return p; }
template <bool B, class T = void> struct en {}; template <class T> struct en<true, T> { typedef T type; };
template <class T, class... A> T *cat(T *p, A &&...a) { return ::new ((void *)p) T(static_cast<A &&>(a)...); }
namespace {	/* (vcc guesses the constructor's name, which clang writes with Tn <enable_if expression>: local symbols) */
template <class X> struct isq { static const bool v = false; };
template <int K> struct Q;
template <int K> struct isq<Q<K>> { static const bool v = true; };
template <int K> struct Q {
	int v; Q *kid; char buf[sizeof(int) + sizeof(void *) * 2 + 8];
	Q(int v) : v(v), kid(0) {}
	template <class O, typename en<isq<O>::v && !__is_same(O, Q), int>::type = 0>
	Q(const O &o) : v(o.v + 1), kid(0) { if (o.kid) kid = cat((Q *)new char[sizeof(Q)], *o.kid); }
};
}
int main() { Q<1> a(3), c(4); a.kid = &c; Q<0> b(a); return b.v == 4 && b.kid && b.kid->v == 5 && !b.kid->kid ? 0 : 1; }
