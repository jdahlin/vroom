// template <...> const D X<T, D>::m = init; (libc++'s deque<_Tp, _Alloc>::__block_size and
// __deque_iterator<...>::__block_size): an instance's member gets its value and, for a use of its address, a
// weak object; the initializer sees the class's members (value_type), constexpr or not
template <class T> struct BS { static const long value = sizeof(T) < 256 ? 4096 / sizeof(T) : 16; };
template <class T, class D> struct It {
	typedef T value_type;
	static const D block;
	D get() const { return block; }
	const D *addr() const { return &block; }
};
template <class T, class D> const D It<T, D>::block = BS<value_type>::value;
template <class T> struct Q {
	static const long n;
	long twice() const { return n * 2; }
	const long *where() const { return &n; }
};
template <class T> constexpr const long Q<T>::n = sizeof(T) * 3;
int main()
{
	It<int, long> i;
	Q<short> q;
	return i.get() == 1024 && *i.addr() == 1024 && q.twice() == 12 && *q.where() == 6 ? 0 : 1;
}
