// [temp.variadic]/7: a pack expansion in the braced-init-list of a class:
// constructor arguments, an initializer-list constructor, an aggregate, and
// the right operand of an assignment ([expr.ass]/8)
namespace std
{ // as libc++'s: the compiler fills in the pointer and the length
template <class E> class initializer_list
{
    const E *begin_;
    decltype(sizeof 0) size_;

  public:
    constexpr initializer_list() noexcept : begin_(nullptr), size_(0) {}
    constexpr const E *begin() const noexcept
    {
        return begin_;
    }
    constexpr const E *end() const noexcept
    {
        return begin_ + size_;
    }
};
} // namespace std

struct Two {
    int value;
    Two() : value(0) {}
    Two(int a, int b) : value(a * 10 + b) {}
};
struct List {
    int value;
    List() : value(0) {}
    List(std::initializer_list<int> l) : value(0)
    {
        for (int x : l)
            value = value * 10 + x;
    }
};
struct Aggregate {
    int a, b, c;
};

template <class... A> int two(A... a)
{
    Two t{a...};
    Two u;
    u = {(a + 1)...};
    return t.value * 100 + u.value;
}
template <class... A> int list(A... a)
{
    List t{a...};
    List u;
    u = {0, (a + 1)...};
    return t.value * 1000 + u.value;
}
template <class... A> int aggregate(A... a)
{
    Aggregate g{a...};
    return g.a * 100 + g.b * 10 + g.c;
}

int main()
{
    if (two(1, 2) != 1223)
        return 1;
    if (list(1, 2, 3) != 123234)
        return 2;
    if (list() != 0)
        return 3;
    if (aggregate(4, 5) != 450)
        return 4;
    return 0;
}
