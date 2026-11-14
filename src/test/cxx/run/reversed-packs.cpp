// std: c++20
// a <=> b through the reversed candidate 0 <=> (b <=> a) (an ordering type shaped as libc++'s, its
// value in __value_), and a pack after unnamed template parameters given explicitly
struct ord {
	signed char __value_;
	static const ord less, equal, greater;
	friend bool operator<(ord o, int) { return o.__value_ == -1; }
	friend bool operator>(ord o, int) { return o.__value_ == 1; }
	friend bool operator==(ord o, int) { return o.__value_ == 0; }
	friend ord operator<=>(int, ord o) { return ord{(signed char)-o.__value_}; }
};
const ord ord::less{-1}, ord::equal{0}, ord::greater{1};
struct S {
	int v;
	friend ord operator<=>(const S &s, int k) { return s.v < k ? ord::less : s.v > k ? ord::greater : ord::equal; }
};

template <class K, bool, class R, class... A> R count(A &&...) { return sizeof...(A); }
template <class K, class, class... A> int count2(A &&...) { return sizeof...(A); }

int main()
{
	S s{5};
	auto a = 3 <=> s, b = 7 <=> s, e = 5 <=> s;
	if (!(a < 0) || !(b > 0) || !(e == 0))
		return 1;
	if (count<int, true, int>(1, 2, 3) != 3 || count2<int, long>('a') != 1)
		return 2;
	return 0;
}
