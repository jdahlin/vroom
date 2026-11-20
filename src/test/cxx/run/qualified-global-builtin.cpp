// [lex.name]/3: a __builtin_ name is declared at global scope as clang does;
// ::__builtin_fabs names it in a namespace's functions.
namespace n {
inline float mag(float x) { return ::__builtin_fabsf(x); }
inline double mag(double x) { return ::__builtin_fabs(x); }
inline long double mag(long double x) { return ::__builtin_fabsl(x); }
} // namespace n

int main()
{
    if (n::mag(-2.0f) != 2.0f)
        return 1;
    if (n::mag(-2.0) != 2.0)
        return 2;
    return 0;
}
