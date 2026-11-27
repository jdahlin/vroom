/* [abi:mangling-compression]: a lambda parameter of a type local to a
   function template specialization names the template by its substitution */
extern "C" int printf(const char *, ...);
template <class T> int f(T t)
{
    struct L { int v; };
    auto g = [](L l) { return l.v; };
    return g(L{(int)t});
}
int h()
{
    struct M { int v; };
    auto g = [](M l) { return l.v; };
    return g(M{2});
}
int main() { printf("%d %d %d\n", f(1), f(2L), h()); }
namespace std {
template <class T> int local_of(T t)
{
    struct L { int v; };
    auto g = [](L l) { return l.v; };
    return g(L{(int)t});
}
}
namespace ns {
template <class T> int local_in(T t)
{
    struct L { int v; };
    auto g = [](L l) { return l.v; };
    return g(L{(int)t});
}
}
int k() { return std::local_of(1) + ns::local_in(2); }
