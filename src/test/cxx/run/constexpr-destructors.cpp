/* Destructors in the constant evaluator ([stmt.dcl]/2, [class.dtor]/9,
   [expr.call]/8, [class.temporary]/4): a local is destroyed when its block
   ends, last declared first, through a break, a return or the next
   iteration; a destructor runs the destructors of its members and bases
   after its body; a by-value parameter is destroyed when the function
   returns; a temporary at the end of its full-expression.  The
   value-initialization of an aggregate with a partial default member
   initializer zero-initializes first ([dcl.init]/8.2). */
#include <stdio.h>

struct Counter {
    int *count;
    constexpr Counter(int *c) : count(c) {}
    constexpr ~Counter() { ++*count; }
    constexpr int get() const { return 5; }
};

struct Order {
    int *log;
    int id;
    constexpr Order(int *l, int i) : log(l), id(i) {}
    constexpr ~Order() { *log = *log * 10 + id; }
};

struct Member {
    Counter m;
    constexpr Member(int *c) : m(c) {}
};

struct Base : Counter {
    constexpr Base(int *c) : Counter(c) {}
    constexpr ~Base() { *count += 10; }
};

struct Partial {
    int x;
    int y = 2;
};

int runtime_count()
{
    int n = 0;
    {
        Counter c(&n);
    }
    return n;
}

constexpr int block_end()
{
    int n = 0;
    {
        Counter c(&n);
    }
    return n;
}

constexpr int still_alive()
{
    int n = 0;
    Counter c(&n);
    return n;
}

constexpr int reverse_order()
{
    int log = 0;
    {
        Order a(&log, 1);
        Order b(&log, 2);
        Order c(&log, 3);
    }
    return log;
}

constexpr int each_iteration()
{
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        Counter c(&n);
        if (i == 1)
            continue;
    }
    return n;
}

constexpr int before_break()
{
    int n = 0;
    while (true) {
        Counter c(&n);
        break;
    }
    return n;
}

constexpr int through_return()
{
    int n = 0;
    {
        Counter c(&n);
        return n;
    }
}

constexpr void take(Counter) {}

constexpr int by_value()
{
    int n = 0;
    take(Counter(&n));
    return n;
}

constexpr int member_destroyed()
{
    int n = 0;
    {
        Member m(&n);
    }
    return n;
}

constexpr int base_after_body()
{
    int n = 0;
    {
        Base b(&n);
    }
    return n;
}

constexpr int array_destroyed()
{
    int n = 0;
    {
        Counter a[3] = {Counter(&n), Counter(&n), Counter(&n)};
    }
    return n;
}

constexpr int temporary()
{
    int n = 0;
    int v = Counter(&n).get();
    return n * 10 + v;
}

constexpr Partial p = Partial();

static_assert(block_end() == 1);
static_assert(still_alive() == 0);
static_assert(reverse_order() == 321);
static_assert(each_iteration() == 3);
static_assert(before_break() == 1);
static_assert(through_return() == 0);
static_assert(by_value() == 1);
static_assert(member_destroyed() == 1);
static_assert(base_after_body() == 11);
static_assert(array_destroyed() == 3);
static_assert(temporary() == 15);
static_assert(p.x == 0 && p.y == 2);

int main()
{
    printf("%d %d %d %d %d %d %d %d\n", runtime_count(), block_end(),
           reverse_order(), each_iteration(), by_value(), base_after_body(),
           array_destroyed(), temporary());
    return 0;
}
