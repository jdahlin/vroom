/* [class.qual]/2, [namespace.udecl]/1: `using T::T;` names a constructor
   and declares no T in the class: T keeps its meaning after it, here as a
   template parameter, an alias and a namespace's class (abseil's
   checked_compare_base) */
extern "C" int printf(const char *, ...);

struct Less {
    int x;
    Less(int v) : x(v) {}
    Less(int a, int b) : x(a * 10 + b) {}
};

template <class Compare> struct checked_base : Compare {
    using Compare::Compare;
    explicit checked_base(Compare c) : Compare(static_cast<Compare &&>(c)) {}
    Compare copy() const {
        Compare c = *this;
        return c;
    }
};

template <class Compare> struct adapter {
    struct checked : checked_base<Compare> {
        using Base = typename checked::checked_base;
        using Base::Base;
        checked(Compare c) : Base(c) {}
        Base *base() { return this; }
    };
};

namespace n
{
template <class T> struct Box {
    T value;
    Box(T v) : value(v) {}
};
} // namespace n

template <class T> struct Derived : n::Box<T> {
    using n::Box<T>::Box;
    n::Box<T> *self() { return this; }
};

struct Plain : Less {
    using Less::Less;
    int twice() const {
        Less l(x * 2);
        return l.x;
    }
};

int main()
{
    checked_base<Less> a(Less(3));
    checked_base<Less> b(1, 2);
    adapter<Less>::checked c(Less(5));
    adapter<Less>::checked d(3, 4);
    Derived<int> e(7);
    Plain f(2, 5);
    printf("%d %d %d %d %d %d\n", a.copy().x, b.x, c.copy().x,
           d.base()->copy().x, e.self()->value, f.twice());
    return 0;
}
