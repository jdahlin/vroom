// [basic.types]/10: a class whose only constexpr constructors are
// constructor templates is a literal type.
template <int N> struct Tag {
};
struct Settings {
    template <int N> constexpr explicit Settings(const Tag<N> &) : v(N) {}
    Settings() = delete;
    Settings(const Settings &) = delete;
    const int v;
};

template <int N> constexpr Tag<N> make() { return Tag<N>(); }

static const Settings *get()
{
    constexpr static Settings k(make<7>());
    return &k;
}

int main() { return get()->v - 7; }
