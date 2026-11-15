// ref-qualified member functions: f() & is not called on an rvalue, f() && not on an lvalue; overloads on
// them; mangled with R and O (json's operator++(int)& and the detection idiom in unit-class_iterator)
template <class T> T &&declval();
struct It {
	int v = 0;
	It operator++(int) & { It r = *this; ++v; return r; }
	int get() & { return 1; }
	int get() && { return 2; }
	int get() const & { return 3; }
};
template <class T> struct det { template <class U> static char t(decltype((declval<U>()++)++) *); template <class U> static long t(...);
				static constexpr bool value = sizeof(t<T>(nullptr)) == 1; };
static_assert(!det<It &>::value, "");
It mk() { return It(); }
int main()
{
	It a;
	const It &ca = a;
	It b = a++;
	return (a.v == 1 && b.v == 0 && a.get() == 1 && mk().get() == 2 && ca.get() == 3 && static_cast<It &&>(a).get() == 2) ? 0 : 1;
}
