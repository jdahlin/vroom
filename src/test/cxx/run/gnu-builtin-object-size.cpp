/* [ext:gnu_library_builtins] size_t __builtin_object_size(const void *,
   int) and __builtin_dynamic_object_size (GCC manual 6.56): the size of the
   object the pointer points into, or the type's unknown value. */
typedef unsigned long size_t;

struct R {
    char head[4];
    char tail[12];
};

static size_t whole(R *r)
{
    return __builtin_object_size(r, 0);
}

static size_t member(R *r)
{
    return __builtin_object_size(r->tail, 1);
}

static size_t unknown(char *p)
{
    return __builtin_object_size(p, 0);
}

static size_t dynamic(char *p)
{
    return __builtin_dynamic_object_size(p, 0);
}

int main()
{
    R r;
    char buf[40];
    if (__builtin_object_size(&r, 0) != sizeof r)
        return 1;
    if (__builtin_object_size(buf + 8, 0) != 32)
        return 2;
    if (member(&r) != 12 && member(&r) != sizeof r)
        return 3;
    if (unknown(buf) != (size_t)-1 && unknown(buf) != 40)
        return 4;
    if (dynamic(buf) != 40 && dynamic(buf) != (size_t)-1)
        return 5;
    if (whole(&r) != (size_t)-1 && whole(&r) != sizeof r)
        return 6;
    return 0;
}
