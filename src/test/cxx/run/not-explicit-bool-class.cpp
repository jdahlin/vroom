extern "C" int printf(const char*, ...);
struct P { int* p; explicit operator bool() const { return p != nullptr; } };
template<class T> int take(const T& v) { return v ? 1 : 0; }
int main() { P a{nullptr}; int x = 3; P b{&x}; printf("%d %d %d\n", take(!a), take(!b), (int)!b); }
