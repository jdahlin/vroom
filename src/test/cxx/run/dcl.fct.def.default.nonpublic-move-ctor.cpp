// [dcl.fct.def.default]: non-public defaulted copy and move constructors.
struct Base {
    int x;
    Base(int v) : x(v) {}
protected:
    Base(const Base &) = default;
    Base(Base &&) = default;
};

struct Derived : Base {
    Derived(int v) : Base(v) {}
    Derived(Derived &&) = default;
};

int main()
{
    Derived a(7);
    Derived b(static_cast<Derived &&>(a));
    return b.x == 7 ? 0 : 1;
}
