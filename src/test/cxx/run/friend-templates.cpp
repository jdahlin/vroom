// std: c++20
// a friend function template defined in its class (an iterator's operator-) called from another
// template: not a member; f(T, T) more specialized than f(T, U); a requires-expression whose
// requirement fails is false, nothing around it ill-formed
template <class I> struct wrap {
	I i;
	template <class J> friend long operator-(const wrap &x, const wrap<J> &y) { return x.i - y.i; }
};
template <class T> long dist(T a, T b) { return b - a; }

template <class T> int pick(T, T) { return 1; }
template <class T, class U> int pick(T, U) { return 2; }

template <class T> concept has_type = requires { typename T::type; };
struct A { using type = int; };
template <class T> int kind(T) { if constexpr (has_type<T>) return 1; else return 2; }
template <class T> struct R { using type = T; };
template <class T> typename R<T>::type viaR(T t) { if constexpr (requires { typename T::nope; }) return t + 1; else return t; }

int main()
{
	int a[4] = {1, 2, 3, 4};
	wrap<int *> b{a}, e{a + 4};
	if (dist(b, e) != 4)
		return 1;
	if (pick(1, 2) != 1 || pick(1, 2L) != 2)
		return 2;
	if (kind(A{}) != 1 || kind(3) != 2 || viaR(5) != 5)
		return 3;
	return 0;
}
