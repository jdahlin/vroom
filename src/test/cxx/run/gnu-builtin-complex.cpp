/* [ext:gnu_library_builtins] _Complex __builtin_complex(real, imag) (GCC
   manual 6.14, "Complex Numbers": the two operands have the same real
   floating type), __real__, __imag__ and the conj, creal, cimag builtins.
   libc++'s <complex> is written with them. */
static _Complex double make(double a, double b)
{
    return __builtin_complex(a, b);
}
static _Complex float makef(float a, float b)
{
    return __builtin_complex(a, b);
}

int main()
{
    _Complex double z = make(1.5, -2.5);
    if (__real__ z != 1.5 || __imag__ z != -2.5)
        return 1;
    _Complex float w = makef(3.0f, 4.0f);
    if (__builtin_crealf(w) != 3.0f || __builtin_cimagf(w) != 4.0f)
        return 2;
    _Complex double c = __builtin_conj(z);
    if (__real__ c != 1.5 || __imag__ c != 2.5)
        return 3;
    constexpr _Complex double k = __builtin_complex(2.0, 3.0);
    static_assert(__real__ k == 2.0 && __imag__ k == 3.0);
    return 0;
}
