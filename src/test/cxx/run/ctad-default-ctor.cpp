/*
 * [over.match.class.deduct]/1.2: implicit deduction guide for a primary class
 * template with no user-declared constructors and default template arguments,
 * e.g. std::less{}.
 * [dcl.init.aggr]/5.1: omitted aggregate member with a default member
 * initializer whose value is built in place (aggregate_built_in_place).
 */

template <typename T = void>
struct Less {
    int tag = 7;
    template <typename A, typename B>
    bool operator()(A a, B b) const
    {
        return a < b;
    }
};

struct NonTrivial {
    int v;
    NonTrivial() : v(42) {}
};

struct Inner {
    int x = 3;
    NonTrivial nt{};
};

struct Outer {
    int a = 1;
    Inner inner{};
};

int main()
{
    auto cmp = Less{};
    if (cmp.tag != 7)
        return 1;
    if (!cmp(1, 2) || cmp(2, 1))
        return 2;

    Outer o{};
    if (o.a != 1 || o.inner.x != 3 || o.inner.nt.v != 42)
        return 3;
    return 0;
}
