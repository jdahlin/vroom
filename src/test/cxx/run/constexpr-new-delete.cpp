/* new and delete in the constant evaluator ([expr.new], [expr.delete],
   [expr.const]/5.18 (C++20): transient allocation): the new-expression's
   object is a heap object that lives until the delete-expression frees it
   (running the destructor), within the evaluation; the allocation
   functions are not called. */
#include <stdio.h>

struct S {
    int v;
    constexpr S(int x) : v(x) {}
};

struct P {
    int a;
    int b;
};

struct D {
    int *count;
    constexpr D(int *c) : count(c) {}
    constexpr ~D() { ++*count; }
};

constexpr int scalar()
{
    int *p = new int(4);
    int r = *p;
    delete p;
    return r;
}

constexpr int class_object()
{
    S *p = new S(7);
    int r = p->v;
    delete p;
    return r;
}

constexpr int aggregate()
{
    P *p = new P{1, 2};
    int r = p->a * 10 + p->b;
    delete p;
    return r;
}

constexpr int two_in_order()
{
    int *a = new int(1);
    int *b = new int(2);
    int r = *a * 10 + *b;
    delete b;
    delete a;
    return r;
}

constexpr int in_a_loop()
{
    int sum = 0;
    for (int i = 0; i < 4; ++i) {
        int *p = new int(i);
        sum += *p;
        delete p;
    }
    return sum;
}

constexpr int destructor_runs()
{
    int n = 0;
    D *p = new D(&n);
    delete p;
    return n;
}

constexpr int modified()
{
    int *p = new int(1);
    *p += 41;
    int r = *p;
    delete p;
    return r;
}

static_assert(scalar() == 4);
static_assert(class_object() == 7);
static_assert(aggregate() == 12);
static_assert(two_in_order() == 12);
static_assert(in_a_loop() == 6);
static_assert(destructor_runs() == 1);
static_assert(modified() == 42);

int main()
{
    printf("%d %d %d %d %d %d %d\n", scalar(), class_object(), aggregate(),
           two_in_order(), in_a_loop(), destructor_runs(), modified());
    return 0;
}
