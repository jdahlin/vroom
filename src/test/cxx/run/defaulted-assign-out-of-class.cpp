// std: c++20
// X& X::operator=(const X&) = default; and (X&&) = default; outside the class define the operators
// ([dcl.fct.def.default]/5; other TUs call them: Skia's SkColorInfo): bases first, then members in order,
// a class member through its operator=, a move moving ([class.copy.assign]/12), *this returned (/14)
static int order[8], norder;
struct sp {
	int *p = nullptr;
	int tag;
	sp &operator=(const sp &o) { p = o.p; order[norder++] = tag; return *this; }
	sp &operator=(sp &&o) { p = o.p; o.p = nullptr; order[norder++] = tag + 10; return *this; }
};
struct Base {
	sp b{nullptr, 1};
};
struct CI : Base {
	CI &operator=(const CI &);
	CI &operator=(CI &&);
	sp cs{nullptr, 2};
	int ct = 3;
	int arr[2] = {4, 5};
};
CI &CI::operator=(const CI &) = default;
CI &CI::operator=(CI &&) = default;
int main()
{
	int x;
	CI a, b;
	b.cs.p = &x, b.ct = 7, b.arr[1] = 9;
	CI *r = &(a = b);
	if (r != &a || a.cs.p != &x || a.ct != 7 || a.arr[1] != 9 || norder != 2 || order[0] != 1 || order[1] != 2)
		return 1;
	CI m;
	m = static_cast<CI &&>(b);
	if (m.cs.p != &x || b.cs.p || norder != 4 || order[2] != 11 || order[3] != 12)
		return 2;
	return 0;
}
