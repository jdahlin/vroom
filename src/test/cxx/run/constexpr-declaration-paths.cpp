// [dcl.constexpr]/1, /8 and /9: constexpr is a declaration specifier.
// A function has normal runtime semantics and its type is unchanged.
namespace numbers {
constexpr int add(int, int);
}
constexpr int numbers::add(int a, int b) { return a + b; }

static constexpr int consume(const int*, int value) noexcept { return value; }
extern "C" constexpr int increment(int value) { return value + 1; }

static int destroyed;

struct Counter {
    int value;
    constexpr Counter(int n);
    constexpr ~Counter() { if (value < 0) ++destroyed; }
    constexpr int current() const { return value; }
    constexpr int plus(int n) const;
    static constexpr int twice(int n) { return n * 2; }
};
constexpr Counter::Counter(int n) : value(n) { }
constexpr int Counter::plus(int n) const { return value + n; }

struct InlineCounter {
    int value;
    constexpr InlineCounter(int n) : value(n) { }
};
struct OutOfLineDestructor {
    int value;
    constexpr ~OutOfLineDestructor();
};
constexpr OutOfLineDestructor::~OutOfLineDestructor() { if (value < 0) ++destroyed; }

constexpr int local_and_branch(int n)
{
    int x = n;
    if (x > 0)
        return x + 1;
    return 0;
}

int forward_call(int n)
{
    constexpr int later(int);
    return later(n);
}
constexpr int later(int n) { return n + 1; }

static_assert(numbers::add(2, 3) == 5);
static_assert(later(4) == 5);

int main()
{
    constexpr int increment(int);
    {
        Counter negative(-1);
        OutOfLineDestructor also_negative = {-2};
    }
    Counter counter(3);
    InlineCounter other(4);
    OutOfLineDestructor d = {5};
    int (*add)(int, int) = numbers::add;
    return add(counter.current(), other.value) + counter.plus(2) +
               Counter::twice(d.value) + increment(0) +
               consume(nullptr, local_and_branch(other.value)) + destroyed +
               forward_call(1) != 32;
}
