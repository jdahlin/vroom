/* Function templates with type parameters: deduction from a call's
   arguments (by value, by reference, through pointers and arrays, a
   forwarding reference), one specialization per argument list, a template
   calling another, recursion, a constexpr template in a constant
   expression, a template of a namespace ([temp.fct], [temp.deduct.call],
   [temp.inst]). */
#include <stdio.h>

struct pair {
    int a, b;
};

template <class T> T larger(T x, T y)
{
    if (x < y)
        return y;
    return x;
}

template <typename T, typename U> T add(T x, U y) { return x + y; }

template <class T> void swap_values(T &x, T &y)
{
    T t = x;
    x = y;
    y = t;
}

template <class T> T sum(const T *p, int n)
{
    T total = 0;
    for (int i = 0; i < n; i++)
        total += p[i];
    return total;
}

template <class T> int size_of(T &&) { return sizeof(T); }

template <class T> T power(T base, int n)
{
    if (n == 0)
        return 1;
    return base * power(base, n - 1);
}

template <class T> T twice_larger(T x, T y) { return add(larger(x, y), larger(x, y)); }

template <class T> constexpr T cube(T x) { return x * x * x; }

template <class T> static int field_sum(const T &v) { return v.a + v.b; }

namespace math {
template <class T> T square(T x) { return x * x; }
long squares() { return square(3L) + square(4L); }
} // namespace math

int main()
{
    int failures = 0;
    if (larger(3, 7) != 7 || larger(2.5, 1.5) != 2.5 || larger('a', 'b') != 'b')
        failures |= 1;
    if (add(1, 2.75) != 3 || add(1.5, 2) != 3.5)
        failures |= 2;
    int i = 1, j = 2;
    swap_values(i, j);
    double d = 1.25, e = 2.5;
    swap_values(d, e);
    if (i != 2 || j != 1 || d != 2.5 || e != 1.25)
        failures |= 4;
    int numbers[4] = {1, 2, 3, 4};
    double halves[3] = {0.5, 1.5, 2.5};
    if (sum(numbers, 4) != 10 || sum(halves, 3) != 4.5)
        failures |= 8;
    long l = 0;
    if (size_of(l) != 8 || size_of(1) != 4 || size_of('c') != 1)
        failures |= 16;
    if (power(2, 10) != 1024 || power(1.5, 2) != 2.25)
        failures |= 32;
    if (twice_larger(4, 9) != 18)
        failures |= 64;
    char bound[cube(2)];
    if (sizeof bound != 8 || cube(i + 1) != 27)
        failures |= 128;
    pair p = {3, 4};
    if (field_sum(p) != 7 || math::squares() != 25)
        failures |= 256;
    printf("%d\n", failures);
    return failures;
}
