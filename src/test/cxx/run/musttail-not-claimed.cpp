// [impl.clang] vcc honours [[clang::musttail]] but does not make other calls
// in tail position jumps, as code selecting on it expects, so it answers
// __has_cpp_attribute(clang::musttail) with 0 and code takes its portable
// path; both compilers print the same either way
extern "C" int printf(const char *, ...);

#if defined(__clang__) && __has_cpp_attribute(clang::musttail)
#define TAIL [[clang::musttail]]
#else
#define TAIL
#endif

static int count(int n, int acc)
{
    if (!n)
        return acc;
    TAIL return count(n - 1, acc + 2);
}

int main()
{
    printf("%d\n", count(1000, 0));
}
