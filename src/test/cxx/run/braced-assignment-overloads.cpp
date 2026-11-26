// [expr.ass]/8: x = {...} of a class with several assignment operators is
// x.operator=({...}), chosen by overload resolution ([over.ics.list],
// [over.ics.rank]/3.1)
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

struct List {
    int value;
    int which;
    List() : value(0), which(0) {}
    List(int a) : value(a), which(0) {}
    List(const List &o) : value(o.value), which(0) {}
    List(std::initializer_list<int> l) : value(0), which(0)
    {
        for (int x : l)
            value += x;
    }
    List &operator=(const List &o)
    {
        value = o.value;
        which = 1;
        return *this;
    }
    List &operator=(List &&o)
    {
        value = o.value;
        which = 2;
        return *this;
    }
    List &operator=(std::initializer_list<int> l)
    {
        value = 0;
        for (int x : l)
            value = value * 10 + x;
        which = 3;
        return *this;
    }
};

struct Pair {
    int value;
    int which;
    Pair(int a, int b) : value(a * 10 + b), which(0) {}
    Pair(const Pair &o) : value(o.value), which(0) {}
    Pair &operator=(const Pair &o)
    {
        value = o.value;
        which = 1;
        return *this;
    }
    Pair &operator=(const char *)
    {
        which = 2;
        return *this;
    }
};

int main()
{
    List a;
    int two = 2;
    a = {1, two, 3};
    if (a.value != 123 || a.which != 3)
        return 1;
    a = {a.value};
    if (a.value != 123 || a.which != 3)
        return 2;
    Pair p(0, 0);
    p = {4, two};
    if (p.value != 42 || p.which != 1)
        return 3;
    return 0;
}
