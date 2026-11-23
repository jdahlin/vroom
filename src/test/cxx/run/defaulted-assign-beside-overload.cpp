// Defaulted copy and move assignment operators alongside a non-special
// operator= overload ([class.copy.assign]/2, /3), as in std::optional.

struct NullTag {};

struct Opt {
    int val = 0;
    bool has = false;

    Opt(int v, bool h) : val(v), has(h) {}
    constexpr Opt(const Opt&) = default;
    constexpr Opt(Opt&&) = default;

    Opt& operator=(NullTag) noexcept {
        has = false;
        return *this;
    }
    Opt& operator=(const Opt&) = default;
    Opt& operator=(Opt&&) = default;
};

int main() {
    Opt a(42, true);
    Opt b(a);
    return (b.has && b.val == 42) ? 0 : 1;
}
