// __builtin_align_up, __builtin_align_down and __builtin_is_aligned: the operand's type, clang++'s values
template <class T, class U> struct same { static constexpr bool value = false; };
template <class T> struct same<T, T> { static constexpr bool value = true; };
int *p;
void *v;
unsigned long n;
short s;
long long f(unsigned a)
{
    static_assert(same<decltype(__builtin_align_up(p, 16)), int *>::value);
    static_assert(same<decltype(__builtin_align_down(v, 8)), void *>::value);
    static_assert(same<decltype(__builtin_align_up(n, 4096u)), unsigned long>::value);
    static_assert(same<decltype(__builtin_align_down(s, 2)), short>::value);
    static_assert(same<decltype(__builtin_is_aligned(p, 4)), bool>::value);
    static_assert(same<decltype(__builtin_is_aligned(n, a)), bool>::value);
    return 0;
}
