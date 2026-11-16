// f<S>("text") with f(const T&) and f(T&&): T given explicitly, nothing to deduce, the argument converts
// (json's j.template value<std::string>("k", "default"))
struct S { const char *p; int how; S(const char *p) : p(p), how(0) {} };
template <class T> int f(const T &t) { return 1; }
template <class T> int g(T &&t) { return t.p[0]; }
template <class T, class U> int h(const T &, U u) { return (int)u; }
int main() { return f<S>("x") == 1 && g<S>("y") == 'y' && h<S>("z", 3) == 3 ? 0 : 1; }
