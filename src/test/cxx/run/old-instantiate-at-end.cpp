// a function template instance wanted before the template's definition (a namespace-scope initializer
// instantiating a class whose constructor names func<T>, as doctest's TEST_CASE_TEMPLATE): its body made at
// the end of the TU, a point of instantiation
template <class... T> struct tup {};
extern "C" int printf(const char *, ...);

typedef void (*fn)();
struct TC { fn f; TC(fn g, const char *) : f(g) {} };
static void regt(const TC &t) { t.f(); }
template <typename T> static void func();
namespace {
template <typename Tuple> struct iter;
template <typename Type, typename... Rest> struct iter<tup<Type, Rest...>> {
  iter(const char *file) { regt(TC(func<Type>, file)); iter<tup<Rest...>>{file}; }
};
template <> struct iter<tup<>> { iter(const char *) {} };
}
static int reg = (iter<tup<char, long>>("x"), 0);
template <typename T> static void func() { printf("%d\n", (int)sizeof(T)); }
int main() { return 0; }
