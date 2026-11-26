/* [dcl.fct.def.delete]/1, [dcl.fct.spec]: a deleted constructor declared
   explicit; [over.match.ctor]: copy-initialization does not consider it */
extern "C" int printf(const char *, ...);

struct reference
{
    const int *p;
    constexpr explicit reference(int &x) : p(&x) {}
    explicit reference(const int &&x) = delete;
};

struct number
{
    int v;
    number(int x) : v(x) {}
    explicit number(char) = delete;
    explicit number() = delete;
};

static int value(number n)
{
    return n.v;
}

int main()
{
    int x = 7;
    reference r(x);
    char c = 3;
    number n = c;
    printf("%d %d %d\n", *r.p, n.v, value(c));
    printf("%d %d\n", __is_constructible(reference, int &),
           __is_constructible(reference, int));
    return 0;
}
