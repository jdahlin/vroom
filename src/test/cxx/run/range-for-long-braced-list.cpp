// for (const auto *s : {"...", ...}) over more than 64 elements (json's unit-testsuites: lists of up to 195 file names)
namespace std {
template <class E> class initializer_list {
	const E *__begin_; unsigned long __size_;
public:
	typedef E value_type;
	constexpr const E *begin() const { return __begin_; }
	constexpr const E *end() const { return __begin_ + __size_; }
};
}
int main()
{
	int n = 0, len = 0;
	for (const auto *s : {"x", "xx", "xxx", "xxxx", "xxxxx", "x", "xx", "xxx", "xxxx", "xxxxx", "x", "xx", "xxx", "xxxx", "xxxxx", "x", "xx", "xxx", "xxxx", "xxxxx", "x", "xx", "xxx", "xxxx", "xxxxx", "x", "xx", "xxx", "xxxx", "xxxxx", "x", "xx", "xxx", "xxxx", "xxxxx", "x", "xx", "xxx", "xxxx", "xxxxx", "x", "xx", "xxx", "xxxx", "xxxxx", "x", "xx", "xxx", "xxxx", "xxxxx", "x", "xx", "xxx", "xxxx", "xxxxx", "x", "xx", "xxx", "xxxx", "xxxxx", "x", "xx", "xxx", "xxxx", "xxxxx", "x", "xx", "xxx", "xxxx", "xxxxx"}) {
		n++;
		for (const char *p = s; *p; p++)
			len++;
	}
	return n == 70 && len == 14 * (1 + 2 + 3 + 4 + 5) ? 0 : 1;
}
