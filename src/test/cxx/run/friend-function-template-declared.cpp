// std: c++20
// [temp.friend]/1: template<class T> friend R f(T); in a class that is not a template: every
// specialization of f is a friend (private members named), also when the template was declared
// before the class and defined after it, and when the class declares it first
struct E;
template <class T> int peek(T);
struct E {
	template <class T> friend int peek(T);
	template <class T> friend E make(T);
	template <class T> friend int twice(const E &, T);
  private:
	int hidden = 5;
	E() {}
};
template <class T> int peek(T)
{
	E e;
	return e.hidden;
}
template <class T> E make(T)
{
	return E();
}
template <class T> int twice(const E &e, T t)
{
	return e.hidden + int(t);
}
int main()
{
	if (peek(1) != 5 || peek(2.0) != 5)
		return 1;
	E e = make('c');
	if (twice(e, 3) != 8)
		return 2;
	return 0;
}
