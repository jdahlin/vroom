// [basic.lookup.argdep]: an unqualified call with arguments where neither
// unqualified lookup nor ADL finds any declaration is ill-formed (and SFINAEs
// away cleanly in template substitution, e.g. fmt's `format_as`).

namespace a {
struct HasFormatAs {
    int v;
};
constexpr int format_as(HasFormatAs x) { return x.v; }
} // namespace a

namespace b {
struct NoFormatAs {
    int v;
};
} // namespace b

template <class T, class = decltype(format_as(T{}))>
constexpr int check(int)
{
    return 1;
}

template <class T>
constexpr int check(long)
{
    return 2;
}

static_assert(check<a::HasFormatAs>(0) == 1);
static_assert(check<b::NoFormatAs>(0) == 2);

int main()
{
    volatile int z = 0;
    if (check<a::HasFormatAs>(z) != 1)
        return 1;
    if (check<b::NoFormatAs>(z) != 2)
        return 2;
    return 0;
}
