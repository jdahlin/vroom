/* [namespace.udecl]/14: a function template overloads a function that a
   using-declaration brought into the namespace before it. */
extern "C" int printf(const char *, ...);
int pick(int v) { return v + 1; }
namespace n {
using ::pick;
template <class T> int pick(T v, int *p) { return static_cast<int>(v) + *p; }
template <class T> int pick(T *) { return 100; }
int inside() { int x = 2; return pick(1) * 1000 + pick(3, &x) + pick(&x); }
}
int main()
{
    int x = 7;
    printf("%d %d %d %d\n", n::pick(4), n::pick('a', &x), n::pick(&x),
           n::inside());
}
