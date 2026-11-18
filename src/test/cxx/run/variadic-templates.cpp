/* Variadic function templates: a template parameter pack deduced from a
   call's trailing arguments (none, some, of different types, through a
   reference pattern), sizeof... of both packs, pack expansions in a call's
   arguments (the pack alone, a call per element), unary and binary folds
   left and right (with the empty pack's values), a constexpr fold in a constant expression ([temp.variadic],
   [temp.deduct.call]/1, [expr.sizeof]/5, [expr.prim.fold]). */
#include <stdio.h>

template <class... Ts> int count(Ts... args)
{
    return sizeof...(Ts) * 10 + sizeof...(args);
}

template <class... Ts> int sum(Ts... a) { return (a + ... + 0); }
template <class... Ts> long left_sum(Ts... a) { return (... + a); }
template <class... Ts> int right_minus(Ts... a) { return (a - ...); }
template <class... Ts> int left_minus(Ts... a) { return (... - a); }
template <class... Ts> int init_minus(Ts... a) { return (100 - ... - a); }
template <class... Ts> bool all(Ts... a) { return (... && a); }
template <class... Ts> bool any(Ts... a) { return (a || ...); }
template <class... Ts> int last(Ts... a) { return (a, ...); }

int g(int a, int b, int c) { return a * 100 + b * 10 + c; }
int h(int x) { return x + 1; }

template <class... Ts> int forward_all(Ts... a) { return g(a...); }
template <class... Ts> int forward_each(Ts... a) { return g(h(a)...); }
template <class... Ts> int forward_some(Ts... a) { return g(7, a...); }

template <class T, class... Ts> T first(T t, const Ts &...rest)
{
    return t + sizeof...(rest);
}

int calls;
int counted(int x)
{
    calls = calls * 10 + x;
    return x;
}
template <class... Ts> int in_order(Ts... a)
{
    calls = 0;
    (counted(a), ...);
    return calls;
}

template <class... Ts> constexpr int product(Ts... a) { return (1 * ... * a); }

template <class... Ts> int by_reference(Ts &...a)
{
    ((a += 1), ...);
    return sizeof...(a);
}

int main(void)
{
    int failures = 0;
    if (count(1, 2.0, 'c') != 33)
        failures |= 1 << 0;
    if (count() != 0)
        failures |= 1 << 1;
    if (sum(1, 2, 3) != 6 || sum() != 0)
        failures |= 1 << 2;
    if (left_sum(1, 2L, 3) != 6)
        failures |= 1 << 3;
    if (right_minus(10, 4, 3) != 9 || left_minus(10, 4, 3) != 3 ||
        init_minus(10, 20) != 70 || init_minus() != 100)
        failures |= 1 << 4;
    if (!all() || all(1, 0) || !all(1, 2) || any() || !any(0, 3))
        failures |= 1 << 5;
    if (forward_all(1, 2, 3) != 123 || forward_each(1, 2, 3) != 234 ||
        forward_some(8, 9) != 789)
        failures |= 1 << 6;
    if (first(5, 1.0, 'x') != 7 || first(5) != 5)
        failures |= 1 << 7;
    if (last(1, 2, 3) != 3)
        failures |= 1 << 9;
    if (in_order(1, 2, 3) != 123)
        failures |= 1 << 10;
    static_assert(product(2, 3, 4) == 24, "fold in a constant expression");
    static_assert(product() == 1, "the empty pack's fold");
    int x = 1, y = 2;
    if (by_reference(x, y) != 2 || x != 2 || y != 3)
        failures |= 1 << 12;
    printf("%d\n", failures);
    return failures;
}
