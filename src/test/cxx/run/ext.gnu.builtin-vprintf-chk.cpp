// __builtin___vprintf_chk (glibc _FORTIFY_SOURCE=2).

extern "C" int __vprintf_chk(int flag, const char *, __builtin_va_list ap)
{
    int x = __builtin_va_arg(ap, int);
    return flag + x;
}

static int call_vp(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int r = __builtin___vprintf_chk(2, fmt, ap);
    __builtin_va_end(ap);
    return r;
}

int main()
{
    if (call_vp("%d", 7) != 9)
        return 1;
    return 0;
}
