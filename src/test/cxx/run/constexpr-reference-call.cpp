// [expr.call]/14: a call of a constexpr function that returns a reference
// is a glvalue a constant expression reads ([expr.const]): std::min's
// shape, and a reference to a member and to an array element.
// [dcl.constexpr]/6: a constexpr member function of a class template that
// calls a function that is not constexpr is not diagnosed for its
// specializations ([temp.inst]/3; clang's -Winvalid-constexpr, SkSpan).
constexpr int x = 7;

constexpr const int &pass(const int &a)
{
    return a;
}

template <class T> constexpr const T &smaller(const T &a, const T &b)
{
    return b < a ? b : a;
}

struct P {
    int a, b;
};
constexpr P p{1, 2};

constexpr const int &second(const P &q)
{
    return q.b;
}

constexpr int table[3] = {4, 5, 6};

constexpr const int &at(int i)
{
    return table[i];
}

template <class T> struct Holder {
    static constexpr unsigned long limit = smaller(sizeof(T) * 100, 64ul);
};

static_assert(pass(x) == 7);
static_assert(pass(3) == 3);
static_assert(smaller(5ul, 2ul) == 2);
static_assert(second(p) == 2);
static_assert(at(2) == 6);
static_assert(Holder<int>::limit == 64);

static int checks;

inline void check(bool empty)
{
    if (empty)
        checks++;
}

template <class T> struct Span {
    T *data;
    unsigned long size;
    constexpr bool empty() const
    {
        return size == 0;
    }
    constexpr T &back() const
    {
        check(empty());
        return data[size - 1];
    }
};

int main()
{
    int v[2] = {8, 9};
    Span<int> s{v, 2};
    if (s.back() != 9 || checks != 0)
        return 1;
    int i = v[0] - 7;
    if (Holder<char>::limit != 64 || at(i) != 5)
        return 2;
    return 0;
}
