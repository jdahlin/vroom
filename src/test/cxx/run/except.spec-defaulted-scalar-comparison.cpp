// std: c++20
// [class.compare.default], [except.spec]: a defaulted comparison of scalars
// without a noexcept-specifier invokes nothing that can throw
struct D {
    int a;
    const char *b[2];
    bool operator==(const D &) const = default;
};

extern const D x, y;
static_assert(noexcept(x == y), "");
static_assert(noexcept(x != y), "");

const D x = {1, {0, 0}}, y = {1, {0, 0}};

int main()
{
    if (!noexcept(x == y))
        return 1;
    return 0;
}
