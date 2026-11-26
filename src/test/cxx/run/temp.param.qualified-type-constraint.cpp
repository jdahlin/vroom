// [temp.param]/4, [expr.prim.req.compound]/1.3: qualified type-constraint
// (e.g. `{ a.base() } noexcept -> ns::convertible_to<long long>;`).

namespace ns {
template <class From, class To>
concept convertible_to = __is_convertible_to(From, To);
}

template <class T>
concept ConvertibleThroughBase = requires(T const a) {
    typename T::BaseType;
    { a.base() } noexcept -> ns::convertible_to<long long>;
};

struct StrongInt {
    using BaseType = int;
    int v;
    constexpr int base() const noexcept { return v; }
};

struct NotStrong {
    int v;
};

static_assert(ConvertibleThroughBase<StrongInt>);
static_assert(!ConvertibleThroughBase<NotStrong>);

int main()
{
    StrongInt s{7};
    if (s.base() != 7)
        return 1;
    return 0;
}
