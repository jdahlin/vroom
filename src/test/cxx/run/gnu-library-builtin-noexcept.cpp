/* [ext:gnu_library_builtins] the C library function that __builtin_fabs
   declares implicitly is noexcept in C++, as clang's: a header's own
   declaration of fabs (glibc's <bits/mathcalls.h> writes it with __THROW,
   noexcept(true)) after a use of the builtin, or without noexcept, is of
   that function. */
double through_builtin(double x)
{
    return __builtin_fabs(x);
}
extern "C" double fabs(double) noexcept(true) __attribute__((__const__));
extern "C" float fabsf(float);

int main()
{
    return through_builtin(-2.0) == 2.0 ? 0 : 1;
}
