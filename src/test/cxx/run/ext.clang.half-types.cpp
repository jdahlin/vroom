/* [ext:clang_float16] _Float16 arithmetic stays in _Float16 */
extern "C" int printf(const char *, ...);

template <class T> struct which {
    static constexpr int value = 0;
};
template <> struct which<_Float16> {
    static constexpr int value = 1;
};
template <class T> __attribute__((noinline)) int tell(T *)
{
    return which<T>::value;
}

int main()
{
    _Float16 a = 1.5f, b = 3;
    int i = 7;
    a += b;
    float f = a * 2.0f;
    double d = a / i;
    printf("%g %g %g %g %d %d\n", (double)a, (double)f, d, (double)(a + i),
           (int)sizeof(a + i), a > b);
    printf("%d\n", tell(&a));
}
