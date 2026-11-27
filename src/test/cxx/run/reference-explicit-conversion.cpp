/* [over.match.ref]/1.1: a direct-initialized reference binds through an
   explicit conversion function. */
extern "C" int printf(const char *, ...);

struct Matrix {
    Matrix() : v(1) {}
    explicit Matrix(int n) : v(n) {}
    int v;
};

template <class T> struct Space;
template <> struct Space<Matrix> {
    Matrix data;
    explicit operator const Matrix &() const { return data; }
};

struct ByValue {
    explicit operator Matrix() const { return Matrix(5); }
};

struct Holder {
    Space<Matrix> transform;
    int f() const
    {
        const Matrix &local(transform);
        return local.v * 10 + (&local == &transform.data);
    }
};

static int g(const Space<Matrix> &s)
{
    const Matrix &a(s);
    return a.v * 10 + (&a == &s.data);
}

int main()
{
    Holder h;
    h.transform.data.v = 3;
    ByValue b;
    const Matrix &c(b);
    Matrix &&d(b);
    printf("%d %d %d %d\n", h.f(), g(h.transform), c.v, d.v);
    return h.f() != 31 || g(h.transform) != 31 || c.v != 5 || d.v != 5;
}
