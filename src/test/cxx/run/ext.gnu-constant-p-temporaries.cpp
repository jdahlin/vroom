/* [ext:gnu_library_builtins] __builtin_constant_p's operand is not
   evaluated: its temporaries are neither made nor destroyed (Catch's
   CHECK) */
extern "C" int printf(const char *, ...);
struct T {
    int v;
    T(int v)
        : v(v)
    {
        printf("make %d\n", v);
    }
    ~T()
    {
        printf("destroy %d\n", v);
    }
};
int main()
{
    (void)__builtin_constant_p(T(1).v == 0);
    int a = __builtin_constant_p(T(2).v);
    printf("-- %d\n", a);
    return a;
}
