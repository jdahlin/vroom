/* [ext:clang_builtin_functions] void __builtin_clear_padding(T *) (clang 20;
   T a trivially copyable class): the bytes of the object that no member
   holds are zeroed.  libc++'s atomic compare-exchange of such a class uses
   it where __has_builtin says it exists (answered 0 here). */
typedef unsigned long size_t;

struct P {
    char c;
    int i;
};

static void clear(P *p)
{
    __builtin_clear_padding(p);
}

int main()
{
    P p;
    __builtin_memset(&p, 0xff, sizeof p);
    p.c = 1;
    p.i = 2;
    clear(&p);
    unsigned char *bytes = (unsigned char *)&p;
    if (bytes[0] != 1 || bytes[1] != 0 || bytes[2] != 0 || bytes[3] != 0)
        return 2;
    return 0;
}
