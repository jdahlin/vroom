/* A class with a destructor in the constant evaluator: the destructor is
   not run by it yet, so an evaluation that would need one is not a constant
   (it answers unknown, not a wrong value); the value-initialization of an
   aggregate with a default member initializer zero-initializes first
   ([dcl.init]/8.2). */
#include <stdio.h>

struct Counter {
    int *count;
    constexpr Counter(int *c) : count(c) {}
    constexpr ~Counter() { ++*count; }
};

int runtime_count()
{
    int n = 0;
    {
        Counter c(&n);
    }
    return n;
}

struct Partial {
    int x;
    int y = 2;
};

constexpr Partial p = Partial();
static_assert(p.x == 0 && p.y == 2);

constexpr int via_function()
{
    Partial q = Partial();
    return q.x + q.y;
}

static_assert(via_function() == 2);

int main()
{
    printf("%d %d\n", runtime_count(), via_function());
    return 0;
}
