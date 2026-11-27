// [impl.clang] vcc claims clang::musttail only for targets where Skia's stages
// were checked to be jumps (not this one, arm64): code takes its portable path,
// and both compilers print the same either way
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
