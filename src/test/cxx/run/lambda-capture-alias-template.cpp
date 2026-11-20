/* a lambda in an alias template's decltype (C++20): the closure type is
   made where the alias is substituted, so clang mangles it in that
   function (_ZZ4mainENK3$_0clEi) */
#define check(e) do { if (!(e)) __builtin_abort(); } while (0)

int zero = 0;

template <class T> using alias = decltype([](T x) { return x; });

int main()
{
    alias<int> l;
    check(l(5 + zero) == 5);
    return 0;
}
