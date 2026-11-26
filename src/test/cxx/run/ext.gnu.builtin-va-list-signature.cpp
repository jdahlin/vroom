// Compiler builtins whose signature contains a va_list ('V') parameter,
// such as __builtin___vsnprintf_chk used by glibc _FORTIFY_SOURCE=2.

extern "C" int __vsnprintf_chk(char *buf, unsigned long n, int, unsigned long,
                               const char *, __builtin_va_list ap)
{
    int a = __builtin_va_arg(ap, int);
    int b = __builtin_va_arg(ap, int);
    if (n < 4)
        return 0;
    buf[0] = static_cast<char>('0' + a);
    buf[1] = '+';
    buf[2] = static_cast<char>('0' + b);
    buf[3] = '\0';
    return 3;
}

static int fmt_buf(char *buf, unsigned long n, const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int r = __builtin___vsnprintf_chk(buf, n, 0, n, fmt, ap);
    __builtin_va_end(ap);
    return r;
}

int main()
{
    char buf[32];
    int n = fmt_buf(buf, sizeof(buf), "%d+%d", 2, 3);
    if (n != 3)
        return 1;
    if (buf[0] != '2' || buf[1] != '+' || buf[2] != '3' || buf[3] != '\0')
        return 2;
    return 0;
}
