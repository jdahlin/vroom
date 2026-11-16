// T{"~"}: an initializer_list<char> constructor is not viable for a char array; the const char * one is
namespace std {
template <class E> class initializer_list {
	const E *b; unsigned long n;
public:
	typedef E value_type;
	constexpr const E *begin() const { return b; }
	constexpr unsigned long size() const { return n; }
};
}
struct S {
	int how; char c;
	S(const char *p) : how(1), c(p[0]) {}
	S(std::initializer_list<char> l) : how(2), c(l.size() ? *l.begin() : 0) {}
};
int main()
{
	S a{"~"};
	auto d = S{"/"};
	return a.how == 1 && a.c == '~' && d.how == 1 && d.c == '/' ? 0 : 1;
}
