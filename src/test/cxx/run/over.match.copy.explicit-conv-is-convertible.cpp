// [over.match.copy]/1.2: explicit conversion functions are not candidates
// for implicit conversion sequences (so __is_convertible is false).

struct Target {
    int x;
};

struct ExplicitFrom {
    explicit operator Target() const { return Target{1}; }
};

struct ImplicitFrom {
    operator Target() const { return Target{2}; }
};

static_assert(!__is_convertible(ExplicitFrom, Target));
static_assert(__is_convertible(ImplicitFrom, Target));

int main()
{
    Target a = Target(ExplicitFrom{});
    Target b = ImplicitFrom{};
    if (a.x != 1 || b.x != 2)
        return 1;
    return 0;
}
