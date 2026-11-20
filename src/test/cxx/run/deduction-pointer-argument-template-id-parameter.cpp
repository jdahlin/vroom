// std: c++20
// a call whose argument is a pointer, against a function template whose parameter is a
// simple-template-id ([temp.deduct.call]/4.3, [temp.deduct.call]/4.2): a pointer is not a derived
// class, and the parameter is no pointer, so deduction fails and the other overload is chosen,
// also in a detection idiom whose argument types are dependent
template <class T> struct Box { T v; };
template <class T> int pick(Box<T> &) { return 1; }
template <class T> int pick(T *&) { return 2; }
template <class T> T &&make();
template <class...> using void_t = void;
template <class T, class = void> constexpr bool can_pick = false;
template <class T> constexpr bool can_pick<T, void_t<decltype(pick(make<T>()))>> = true;
static_assert(can_pick<char *&>);
int main()
{
	char c = 0;
	char *p = &c;
	if (pick(p) != 2)
		return 1;
	return 0;
}
