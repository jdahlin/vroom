// [dcl.fct.def.default]/2: a protected defaulted copy or move assignment
// operator, not trivial, used by the class and by a derived class's
// implicit one.
struct Count {
    int n = 0;
    Count() = default;
    Count(const Count &o) : n(o.n) {}
    Count &operator=(const Count &o)
    {
        n = o.n + 1;
        return *this;
    }
    Count &operator=(Count &&o)
    {
        n = o.n + 100;
        return *this;
    }
};

class Base
{
  public:
    Count c;
    void clear() { *this = {}; }
    void take(const Base &o) { *this = o; }

  protected:
    Base() = default;
    Base(const Base &) = default;
    Base(Base &&) = default;
    Base &operator=(const Base &) = default;
    Base &operator=(Base &&) = default;
};

struct Derived : Base {
    int extra = 0;
};

int main()
{
    Derived a, b;
    a.c.n = 5;
    b = a;
    int r = b.c.n;           // 6
    b = static_cast<Derived &&>(a);
    r = r * 1000 + b.c.n;    // 105
    b.take(a);
    r = r * 1000 + b.c.n;    // 6
    b.clear();
    if (r != 6105006 || b.c.n != 100)
        return 1;
    return 0;
}
