// GNU __attribute__((...)) after cv-qualifiers in a pointer declarator,
// as used by glibc _FORTIFY_SOURCE=2 (__fortify_clang_overload_arg).

static int read_first(const char *__restrict const __attribute__((__unused__)) p)
{
    return p ? p[0] : 0;
}

int main()
{
    const char s[] = "A";
    if (read_first(s) != 'A')
        return 1;
    if (read_first(nullptr) != 0)
        return 2;
    return 0;
}
