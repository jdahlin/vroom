// X(X&&) = default and X& operator=(X&&) = default, trivial, as Skia's
// SkNoncopyable: a move copies the bytes ([class.copy.ctor]/11), the
// implicit copy constructor is deleted ([class.copy.ctor]/6).
extern "C" int printf(const char *, ...);

class Noncopyable {
public:
    Noncopyable() = default;
    Noncopyable(Noncopyable &&) = default;
    Noncopyable &operator=(Noncopyable &&) = default;

private:
    Noncopyable(const Noncopyable &) = delete;
    Noncopyable &operator=(const Noncopyable &) = delete;
};

struct Pair {
    int a, b;
    Pair() = default;
    Pair(Pair &&) = default;
};

class Owner : Noncopyable {
public:
    int value;
};

int main()
{
    Noncopyable n;
    Noncopyable m(static_cast<Noncopyable &&>(n));
    Pair p;
    p.a = 3;
    p.b = 4;
    Pair q(static_cast<Pair &&>(p));
    Pair r = static_cast<Pair &&>(q);
    Owner o;
    o.value = 7;
    printf("%d %d %d\n", r.a, r.b, o.value);
    return r.a + r.b + o.value == 14 ? 0 : 1;
}
