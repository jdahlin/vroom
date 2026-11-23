// a function template declared, used as a value (fn f = func<T>), then defined (doctest's TEST_CASE_TEMPLATE)
extern "C" int printf(const char *, ...);
typedef void (*fn)();
template <typename T> void func();
template <typename T> struct reg { reg() { fn f = func<T>; f(); } };
template <typename T> void func() { printf("%d\n", (int)sizeof(T)); }
int main() { reg<long> r; reg<char> q; return 0; }
