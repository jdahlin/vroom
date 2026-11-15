// a template-id naming a function as a constructor's argument for a function-pointer parameter, in a class
// template's member (doctest's TEST_CASE_TEMPLATE: TestCase(func<Type>, ...)), direct and as a functional cast
typedef void (*fn)();
struct TC { fn f; int line; TC(fn g, int l) : f(g), line(l) {} };
int hits;
template <class T> static void func() { hits += sizeof(T); }
template <class T> struct reg { reg() { TC t(func<T>, 1); t.f(); TC u = TC(func<T>, 2); u.f(); } };
int main() { reg<int> a; reg<char> b; return hits == 10 ? 0 : 1; }
