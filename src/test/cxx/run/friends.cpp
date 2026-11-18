/* [class.friend]: friend classes (declared before and after the
   befriending class, by type-name), friend functions declared before, first
   declared by the friend declaration and found by argument-dependent
   lookup ([basic.lookup.argdep]/4.3), protected members through a friend
   of a derived class ([class.protected]) */
namespace N {
class Counter;
int peek(const Counter &c);
class Counter {
    int n;
    static int made;
    friend class Inspector;
    friend struct Bumper;
    friend int peek(const Counter &c);
    friend int twice(const Counter &c);

  public:
    Counter(int v) : n(v) { made++; }
};
int Counter::made = 0;
struct Bumper {
    void bump(Counter &c) { c.n++; }
    struct Inner {
        int read(const Counter &c) { return c.n; }
    };
};
class Inspector {
  public:
    static int made() { return Counter::made; }
};
int peek(const Counter &c) { return c.n; }
int twice(const Counter &c) { return 2 * c.n; }
} // namespace N

class Base {
  protected:
    int p = 5;
};
class Derived : public Base {
    friend int get_p(Derived &d);
};
int get_p(Derived &d) { return d.p; }

class Self {
    int v = 9;
    friend Self;
    friend class Other;

  public:
    int copy(const Self &s) { return s.v; }
};
class Other {
  public:
    int read(const Self &s) { return s.v; }
};

int call_twice(const N::Counter &c) { return twice(c); }

int main()
{
    N::Counter c(4);
    N::Bumper b;
    b.bump(c);
    int bad = 0;
    if (N::peek(c) != 5) bad |= 1;
    if (call_twice(c) != 10) bad |= 2;
    N::Bumper::Inner inner;
    if (inner.read(c) != 5) bad |= 4;
    if (N::Inspector::made() != 1) bad |= 8;
    Derived d;
    if (get_p(d) != 5) bad |= 16;
    Self s;
    Other o;
    if (o.read(s) != 9 || s.copy(s) != 9) bad |= 32;
    return bad;
}
