/* cxx-run-flags: -std=c++20 */
/* [over.match.list]: a member class of a template specialization that
   inherits constructor templates only, over a base named with a class
   nested in a class still being defined (SkTHash's Pair of std::pair);
   unevaluated here, since vcc emits no inheriting constructor symbol */
extern "C" int printf(const char *, ...);

template <class A, class B> struct Two {
    A first;
    B second;
    template <class U, class V> Two(U &&u, V &&v) : first(u), second(v) {}
};

template <class V> struct Map {
    struct Pair : Two<int, V> {
        using Two<int, V>::Two;
    };
    static char take(Pair);
    unsigned long size(int k, V v)
    {
        return sizeof(take({k, v}));
    }
};

class Outer {
public:
    struct Counts {
        int a = 3;
        int b = 5;
    };
    Map<Counts> map;
    unsigned long run();
};

unsigned long Outer::run()
{
    return map.size(1, Counts{});
}

int main()
{
    Outer o;
    printf("%lu\n", o.run());
    return 0;
}
