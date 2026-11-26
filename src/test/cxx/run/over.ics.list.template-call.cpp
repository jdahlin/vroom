// Overloaded function template call with a braced-init-list argument
// ([over.ics.list], [temp.over]): template_call must pass the written
// arguments to best_viable_function so braced-init-list parameters like
// std::initializer_list<T> are probed instead of returning unknown viability.

namespace std {
template <class E>
class initializer_list {
    const E *p;
    __SIZE_TYPE__ n;
    constexpr initializer_list(const E *a, __SIZE_TYPE__ b) : p(a), n(b) {}

  public:
    constexpr initializer_list() : p(nullptr), n(0) {}
    constexpr __SIZE_TYPE__ size() const { return n; }
    constexpr const E *begin() const { return p; }
    constexpr const E *end() const { return p + n; }
};

template <class T>
constexpr const T &max(const T &a, const T &b)
{
    return a < b ? b : a;
}

template <class T>
constexpr T max(initializer_list<T> il)
{
    const T *it = il.begin();
    T res = *it;
    for (++it; it != il.end(); ++it) {
        if (res < *it)
            res = *it;
    }
    return res;
}
} // namespace std

struct Range {
    const unsigned char division;
};

static constexpr Range R1{1};
static constexpr Range R2{3};
static constexpr Range R3{4};
static constexpr unsigned char MAX_DIV =
    std::max({R1.division, R2.division, R3.division});

int main()
{
    if (MAX_DIV != 4)
        return 1;
    int a = 10, b = 25, c = 17;
    if (std::max({a, b, c}) != 25)
        return 2;
    if (std::max(a, c) != 17)
        return 3;
    return 0;
}
