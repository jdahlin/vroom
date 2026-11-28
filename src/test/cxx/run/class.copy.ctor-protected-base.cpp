/* [class.copy.ctor]/10, [class.protected]: a protected copy or move
   constructor of a base class is accessible for the base subobject, so the
   derived class's implicit copy constructor is not deleted (Catch2's
   ResultValueBase). */
class Base {
public:
    int n;
    explicit Base(int v) : n(v) {}

protected:
    Base(const Base &o) : n(o.n + 1) {}
};

class Derived : public Base {
public:
    int m = 5;
    explicit Derived(int v) : Base(v) {}
};

static_assert(__is_constructible(Derived, const Derived &));

int main()
{
    Derived a(1);
    Derived b = a;
    Derived c = b;
    if (a.n != 1 || b.n != 2 || c.n != 3 || c.m != 5)
        return 1;
    return 0;
}
