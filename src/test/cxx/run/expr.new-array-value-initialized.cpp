// [expr.new]: an array new with () or {} value-initializes each element
// ([dcl.init]/8, /11), for a constant bound and one computed at run time.
// The storage is filled before, so that zeros are the initialization's.
using size_t = decltype(sizeof 0);

static unsigned char arena[512];
void *operator new[](size_t n)
{
    for (size_t i = 0; i < sizeof arena; i++)
        arena[i] = 0xa5;
    if (n > sizeof arena)
        return nullptr;
    return arena;
}
void operator delete[](void *) noexcept
{
}

struct plain {
    int a;
    char b;
};
struct members {
    int a;
    int b = 7;
};
struct constructed {
    int a;
    constructed() : a(3)
    {
    }
};
struct destroyed {
    int a;
    ~destroyed()
    {
    }
};

template <typename T> static T *make(size_t n)
{
    return new T[n]();
}

int main()
{
    volatile size_t n = 5;
    int *i = make<int>(n);
    for (size_t k = 0; k < n; k++)
        if (i[k])
            return 1;
    delete[] i;
    char *c = new char[9]{};
    for (int k = 0; k < 9; k++)
        if (c[k])
            return 2;
    delete[] c;
    plain *p = new plain[n]();
    for (size_t k = 0; k < n; k++)
        if (p[k].a || p[k].b)
            return 3;
    delete[] p;
    members *m = make<members>(n);
    for (size_t k = 0; k < n; k++)
        if (m[k].a || m[k].b != 7)
            return 4;
    delete[] m;
    constructed *t = new constructed[n]{};
    for (size_t k = 0; k < n; k++)
        if (t[k].a != 3)
            return 5;
    delete[] t;
    destroyed *d = make<destroyed>(n);
    for (size_t k = 0; k < n; k++)
        if (d[k].a)
            return 6;
    delete[] d;
    return 0;
}
