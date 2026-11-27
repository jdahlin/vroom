// [dcl.spec.auto]/12: an operator function template with a deduced placeholder
// return type must have its definition instantiated when selected by overload
// resolution so its return type is deduced before the call is formed.

namespace {

struct Box {
    int v;
};

template <typename F>
struct Pipe {
    F fn;
};

template <typename T, typename F>
auto operator|(T x, Pipe<F> p)
{
    return p.fn(x);
}

struct Add {
    int k;
    int operator()(int x) const { return x + k; }
};

struct Mul {
    int k;
    int operator()(int x) const { return x * k; }
};

struct Holder {
    int val;
    auto computed() const
    {
        return this->val | Pipe<Add>{Add{5}} | Pipe<Mul>{Mul{3}};
    }
    template <typename T>
    auto operator+(T rhs) const
    {
        return val + rhs;
    }
};

template <typename T, typename U>
struct BaseImpl {
    T a;
    U b;
    BaseImpl(T x, U y) : a(x), b(y) {}
};

template <typename T>
using BaseAlias = BaseImpl<T, int>;

template <typename T>
struct Derived : BaseAlias<T> {
    using BaseAlias<T>::BaseAlias;
};

template <typename T>
decltype(auto) make_derived(T x, int y)
{
    return Derived<T>(x, y);
}

} // namespace

int main()
{
    Holder h{7};
    if (h.computed() != 36)
        return 1;
    if ((h + 10) != 17)
        return 2;
    if ((h + 2.5) != 9.5)
        return 3;
    using R = decltype(1 | Pipe<Add>{Add{2}});
    R r = 42;
    if (r != 42)
        return 4;
    auto d = make_derived(11, 22);
    if (d.a != 11 || d.b != 22)
        return 5;
    return 0;
}
