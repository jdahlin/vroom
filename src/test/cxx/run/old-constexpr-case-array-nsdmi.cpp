// std: c++20
// A constexpr member function's call as a case label (case char_traits<char>::eof(): in json's lexer), and
// default member initializers of arrays (int a[3] = {1, 2, 3}; char s[4] = "abc").
struct T { static constexpr int eof() noexcept { return -1; } static constexpr int nl() noexcept { return T::eof() + 11; } };

int classify(int c)
{
	switch (c) {
	case T::eof(): return 1;
	case T::nl(): return 2;
	case '\0': return 3;
	default: return 0;
	}
}

struct V { int a[4] = {1, 2}; char s[4] = "abc"; int n = 7; };

int main()
{
	int r = 0, bit = 1;
	V v;

	r |= (classify(-1) != 1 || classify(10) != 2 || classify(0) != 3 || classify(5)) * bit, bit <<= 1;
	r |= (v.a[0] != 1 || v.a[1] != 2 || v.a[3] != 0) * bit, bit <<= 1;
	r |= (v.s[2] != 'c' || v.s[3] || v.n != 7) * bit, bit <<= 1;
	return r;
}
