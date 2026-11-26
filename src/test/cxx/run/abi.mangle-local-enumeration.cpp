/* [abi:mangling-scope], [abi:mangling.named]: enumerations of a block
   scope, and local classes and enumerations with no name, in a symbol; the
   unnamed ones of a function that is neither inline nor a specialization
   as [impl.clang]'s $_N */
template <class T> int f(T) { return 1; }

inline int in_inline()
{
    int r = 0;
    {
        enum E { a };
        r += f(a);
    }
    {
        enum E { a };
        r += f(a);
    }
    {
        enum { x };
        r += f(x);
    }
    {
        struct {
            int h() { return 2; }
        } s;
        r += f(s) + s.h();
    }
    {
        enum { y };
        r += f(y);
    }
    auto l = [] { return 1; };
    r += f(l);
    struct L {
        enum In { q };
        enum { z };
        static int h() { return f(q) + f(z); }
    };
    return r + L::h();
}

static int in_static()
{
    struct {
        int h() { return 2; }
    } v;
    struct {
        int h() { return 2; }
    } w;
    enum { x };
    auto l = [] { return 1; };
    return v.h() + w.h() + f(x) + f(w) + f(l);
}

int in_external()
{
    enum c_enum { value_1 };
    enum class cpp_enum { value_1 };
    enum { t };
    return f(c_enum::value_1) + f(cpp_enum::value_1) + f(t);
}

template <class U> int in_template()
{
    enum E { a };
    enum { b };
    return f(a) + f(b);
}

struct M {
    int m()
    {
        enum { x };
        enum N { n };
        return f(x) + f(n);
    }
};

int main()
{
    M m;
    int r = in_inline() + in_static() + in_external() + in_template<int>() +
            m.m();
    return r - 27;
}
