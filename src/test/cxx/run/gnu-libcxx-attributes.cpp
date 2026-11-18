/* libc++'s _LIBCPP_HIDE_FROM_ABI: hidden visibility, excluded from
   explicit instantiation, and an ABI tag in the mangled name; on
   functions, member functions, constructors and function templates */
#define HIDE                                                                 \
    __attribute__((__visibility__("hidden")))                                \
    __attribute__((__exclude_from_explicit_instantiation__))                 \
    __attribute__((__abi_tag__("nqe220106")))

extern "C" int printf(const char *, ...);

HIDE int twice(int x)
{
    return 2 * x;
}

struct counter {
    int n;
    HIDE counter() : n(1) {}
    HIDE int add(int k)
    {
        n += k;
        return n;
    }
    HIDE static int zero() { return 0; }
};

template <class T> HIDE T largest(T a, T b)
{
    return a < b ? b : a;
}

/* an explicit instantiation leaves the excluded members out: get is
   emitted only because main uses it, unused never */
template <class T> struct box {
    T v;
    HIDE T get() { return v; }
    HIDE T unused() { return v + 1; }
    T plain() { return v; }
};
template struct box<int>;

int main()
{
    box<int> b = {5};
    if (b.get() + b.plain() != 10)
        return 2;
    counter c;
    c.add(twice(3));
    printf("%d %d %ld\n", c.n, counter::zero(), largest(3L, 9L));
    return c.n == 7 && largest(2, 1) == 2 ? 0 : 1;
}
