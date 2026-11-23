/* Names libc++'s headers use in their declarations and bodies:
   [temp.deduct.type]/8's T[i], a function template's parameter whose array
   bound is a non-type template-parameter (__iterator/access.h's
   begin(_Tp (&)[_Np])); [ext:gnu_library_builtins] __builtin_fabsf,
   __builtin_fabs (__math/abs.h) and __builtin_is_constant_evaluated
   ([meta.const.eval]/1: true in a manifestly constant-evaluated
   expression, false at run time; __type_traits/is_constant_evaluated.h) */
typedef unsigned long size_t;

template <class T, size_t N> T *first(T (&array)[N])
{
    return array;
}
template <class T, size_t N> T *last(T (&array)[N])
{
    return array + N;
}
template <int N> int count(const char (&)[N])
{
    return N;
}

inline float magnitude(float x)
{
    return __builtin_fabsf(x);
}
template <class = int> double magnitude(double x)
{
    return __builtin_fabs(x);
}

constexpr bool constant_evaluated()
{
    return __builtin_is_constant_evaluated();
}
constexpr int either()
{
    return __builtin_is_constant_evaluated() ? 7 : 3;
}
static_assert(constant_evaluated(), "manifestly constant-evaluated");
constexpr int seven = either();
int global = either();

int main()
{
    int a[5] = {1, 2, 3, 4, 5};
    long b[2] = {10, 20};
    int sum = 0;
    for (int *p = first(a); p != last(a); p++)
        sum += *p;
    if (sum != 15 || *first(b) != 10 || last(b) - first(b) != 2)
        return 1;
    if (count("abc") != 4)
        return 2;
    if (magnitude(-2.5f) != 2.5f || magnitude(-1.25) != 1.25)
        return 3;
    bool runtime = constant_evaluated();
    int (*call)() = either;
    if (runtime || seven != 7 || call() != 3)
        return 4;
    if (global != 7)
        return 5;
    return 0;
}
