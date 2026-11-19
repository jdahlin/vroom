// [temp.mem]/1 with [temp.constr.decl]: member function templates with a
// requires-clause or a type-constraint (libc++'s ranges::__swap::__fn in
// <__concepts/swappable.h>): the constraints are checked after deduction
// ([temp.deduct]/5), an unsatisfied one removes the candidate
// ([over.match.viable]/3); a noexcept-specifier naming the parameters.
template <class T> concept Small = sizeof(T) <= 4;
template <class T> concept Big = sizeof(T) > 4;
template <class T, class U> concept Same = sizeof(T) == sizeof(U);

struct Pick {
    template <class T>
        requires Small<T>
    constexpr int operator()(T v) const noexcept(noexcept(v + 1))
    {
        return static_cast<int>(v) + 1;
    }
    template <class T>
        requires Big<T>
    constexpr int operator()(T v) const
    {
        return static_cast<int>(v) + 100;
    }
};

struct Typed {
    int base;
    template <Small T> int add(T v) const { return base + static_cast<int>(v); }
    template <Big T> int add(T v) const { return base * static_cast<int>(v); }
    template <class T, class U>
        requires Same<T, U> && Small<T>
    int both(T a, U b) const
    {
        return static_cast<int>(a) - static_cast<int>(b);
    }
};

// (a type-constraint on a namespace-scope function template: the same)
template <Small T, Small U> int sum2(T a, U b) { return a + b; }
template <Big T> int sum2(T a, T b) { return static_cast<int>(a - b); }

int main()
{
    // (operands that are no constants: clang folds a constant call)
    int two = 2;
    long three = 3;
    short four = 4;
    Pick p;
    if (p(two) != 3 || p(three) != 103 || p(four) != 5)
        return 1;
    Typed t{10};
    if (t.add(two) != 12 || t.add(three) != 30 || t.both(two, two) != 0)
        return 2;
    if (sum2(two, four) != 6 || sum2(three, three) != 0)
        return 3;
    return 0;
}
