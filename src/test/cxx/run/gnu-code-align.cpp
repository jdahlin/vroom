// [ext:gnu_attributes] clang's code_align(N) before a loop, in GCC's and the
// standard's syntax: a hint for where the loop's code starts
extern "C" int printf(const char *, ...);

static int sum(int n)
{
    int s = 0;
    __attribute__((code_align(64)))
    for (int i = 0; i < n; i++)
        s += i;
    [[clang::code_align(32)]] while (n > 0)
        s += n--;
    __attribute__((__code_align__(8))) do {
        s++;
    } while (s < 100);
    return s;
}

int main()
{
    printf("%d\n", sum(10));
    return 0;
}
