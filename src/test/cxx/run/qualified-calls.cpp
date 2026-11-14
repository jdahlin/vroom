// X<T>::f(a, b); in a block is a call (a block declares no qualified name: it was dropped as a declaration),
// and a template taking fewer parameters than the call has arguments is not a candidate (min(initializer_list)
// beside min(a, b))
template <class T> struct Traits { static void put(T *p, T v) { *p = v; } };
template <class T> struct IL { const T *b; unsigned long n; };
template <class T> const T &mn(const T &a, const T &b) { return b < a ? b : a; }
template <class T> T mn(IL<T> l) { return l.b[0]; }
static void put3(int *p) { Traits<int>::put(p, 3); }
int main()
{
	int x = 0;

	put3(&x);
	return x == 3 && mn<long>(4, 2) == 2 ? 0 : 1;
}
