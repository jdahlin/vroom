// std: c++98
// before C++11 its keywords are identifiers (libc++'s C++03 mode: typedef __char16_t char16_t, #define decltype __decltype)
typedef __char16_t char16_t;
int constexpr_ = 0;
int f(int nullptr_like) { int alignas = 1, noexcept = 2, thread_local = 3; return alignas + noexcept + thread_local + nullptr_like; }
int main() {
	char16_t c = 65;
	int x = 0;
	__decltype(x) y = 4;
	_Static_assert(sizeof(char16_t) == 2, "");
	return f(0) + y + (c - 65) - 10;
}
