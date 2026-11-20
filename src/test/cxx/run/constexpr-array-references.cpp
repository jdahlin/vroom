/* The constant evaluator on references and pointers to arrays: a reference
   parameter bound to an array ([dcl.init.ref]/5.1), the range-based for
   statement over an array ([stmt.ranged]/1: range, range + N), pointer
   arithmetic, comparison and increment ([expr.add]/4, [expr.rel],
   [expr.eq]) and enumerations as indexes (libc++'s
   __compute_comp_type). */
#include <stdio.h>

template <unsigned N> constexpr int sum(const int (&a)[N])
{
    int s = 0;
    for (int x : a)
        s += x;
    return s;
}

constexpr int first(const int (&a)[3]) { return a[1]; }

constexpr int via_pointer(const int *p, int n)
{
    int s = 0;
    for (const int *e = p + n; p != e; ++p)
        s += *p;
    return s;
}

constexpr int by_ref(const int (&a)[4])
{
    int s = 0;
    for (const auto &x : a)
        s = s * 10 + x;
    return s;
}

constexpr int count_down(const int *p, const int *end)
{
    int n = 0;
    while (end > p) {
        --end;
        n += *end;
    }
    return n + static_cast<int>(end - p);
}

constexpr int local_array()
{
    int a[] = {5, 6, 7};
    int s = 0;
    for (int &x : a)
        x += 1;
    for (auto x : a)
        s += x;
    return s;
}

constexpr int bump(int &r)
{
    r += 5;
    return r;
}

constexpr int through_reference()
{
    int v = 1;
    int &r = v;
    r = 4;
    int w = bump(v);
    return v + w + r;
}

constexpr int swap_index(const int *p) { return 1 [p] + p[2]; }

enum Kind : unsigned { None, Partial, Weak, Strong, Size };

template <unsigned N> constexpr Kind compute(const Kind (&kinds)[N])
{
    int seen[Size] = {};
    for (auto k : kinds)
        ++seen[k];
    if (seen[None])
        return None;
    if (seen[Partial])
        return Partial;
    if (seen[Weak])
        return Weak;
    return Strong;
}

constexpr int null_and_bool(const int *p)
{
    if (p)
        return 1;
    if (p == nullptr)
        return 2;
    return 3;
}

constexpr int a3[] = {1, 2, 3};
constexpr int a4[] = {1, 2, 3, 4};

constexpr int outer()
{
    constexpr int a[] = {1, 2, 3};
    constexpr int s = sum(a);
    constexpr Kind ks[] = {Strong, Weak, Strong};
    constexpr Kind cat = compute(ks);
    static_assert(cat == Weak);
    return s;
}

static_assert(outer() == 6);
static_assert(sum(a3) == 6);
static_assert(sum(a4) == 10);
static_assert(first(a3) == 2);
static_assert(via_pointer(a4, 3) == 6);
static_assert(via_pointer(a4 + 1, 3) == 9);
static_assert(by_ref(a4) == 1234);
static_assert(count_down(a4, a4 + 4) == 10);
static_assert(local_array() == 21);
static_assert(through_reference() == 27);
static_assert(swap_index(a4) == 2 + 3);
static_assert(null_and_bool(nullptr) == 2);
static_assert(null_and_bool(a3) == 1);
constexpr Kind all_strong[] = {Strong, Strong};
constexpr Kind with_none[] = {Strong, None, Weak};
constexpr Kind with_partial[] = {Weak, Partial};
static_assert(compute(all_strong) == Strong);
static_assert(compute(with_none) == None);
static_assert(compute(with_partial) == Partial);

int main()
{
    printf("%d %d %d %d\n", sum(a3), by_ref(a4), local_array(),
           through_reference());
    return 0;
}
