/* [impl.clang] diagnose_if: a call whose condition does not hold (or is not a
   constant expression) is accepted, and the attribute changes nothing */
#define DIAGNOSE_IF(...) __attribute__((diagnose_if(__VA_ARGS__)))
typedef decltype(sizeof 0) size_t;

namespace std
{
template <class T> struct initializer_list {
    const T *ptr;
    size_t elems;
    constexpr size_t size() const
    {
        return elems;
    }
};
} // namespace std

int twice(int n) DIAGNOSE_IF(n < 0, "negative", "error");
int twice(int n)
{
    return n * 2;
}

int half(int n) DIAGNOSE_IF(n == 1, "one", "warning");
int half(int n)
{
    return n / 2;
}

struct Counter {
    int total;
    Counter(int n) DIAGNOSE_IF(n > 100, "too many", "error") : total(n) {}
    int add(int n, int k) DIAGNOSE_IF(n == k, "same", "error")
    {
        return total + n + k;
    }
};

struct Sized {
    int count;
    Sized(std::initializer_list<int> l)
        DIAGNOSE_IF(l.size() == 2, "two", "error")
        DIAGNOSE_IF(l.size() > 3, "many", "error")
        : count(static_cast<int>(l.size()))
    {
    }
};

int main()
{
    volatile int runtime = -5;
    if (twice(4) != 8)
        return 1;
    /* not a constant expression: not diagnosed */
    if (twice(runtime) != -10)
        return 2;
    if (half(8) != 4)
        return 3;
    Counter c(7);
    if (c.add(1, 2) != 10)
        return 4;
    Sized s0{std::initializer_list<int>{}};
    Sized s1{std::initializer_list<int>{1}};
    Sized s3{std::initializer_list<int>{1, 2, 3}};
    if (s0.count != 0 || s1.count != 1 || s3.count != 3)
        return 5;
    return 0;
}
