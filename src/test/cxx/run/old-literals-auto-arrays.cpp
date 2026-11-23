// std: c++20
// Character literals with hex and octal escapes ('\x1F' in json's get_token_string), const auto * from a T *
// (localeconv()), and an aggregate's array member from its own braced list (std::array<char, 4> t = {{...}}).
struct L { char *d; };
static L l0;
L *lc() { return &l0; }

struct A { char e[4]; };
struct B { int x; char e[3]; };
template <class C, unsigned N> struct Arr { C v[N]; };
template <class C> int lit() { Arr<C, 4> t = {{static_cast<C>('t'), static_cast<C>('r'), static_cast<C>('u'), static_cast<C>('e')}}; return t.v[3]; }

int main()
{
	int r = 0, bit = 1;
	char c = 5;

	r |= !(static_cast<unsigned char>(c) <= '\x1F') * bit, bit <<= 1;
	r |= ('\xff' != -1 || '\377' != -1 || '\x7f' != 127 || '\101' != 65 || '\7' != 7 || '\?' != 63) * bit, bit <<= 1;
	const auto *p = lc();
	r |= (p != &l0) * bit, bit <<= 1;
	A a = {{1, 2, 3, 4}};
	r |= (a.e[3] != 4) * bit, bit <<= 1;
	B b = {5, {1, 2}};
	r |= (b.e[1] != 2 || b.e[2] || b.x != 5) * bit, bit <<= 1;
	r |= (lit<char>() != 'e') * bit, bit <<= 1;
	return r;
}
