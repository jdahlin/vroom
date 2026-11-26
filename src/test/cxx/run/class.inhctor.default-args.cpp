// [namespace.udecl], [class.inhctor.init]: inheriting a base class constructor
// that has default arguments (e.g. OpenTTD's `SpecializedResolverObject`).

struct Base {
    int a;
    int b;
    int c;
    Base(int a, int b = 10, int c = 20) : a(a), b(b), c(c) {}
};

struct Derived : Base {
    using Base::Base;
    int sum() const { return a + b + c; }
};

int main()
{
    Derived d1(1);
    Derived d2(1, 2);
    Derived d3(1, 2, 3);
    if (d1.sum() != 31 || d2.sum() != 23 || d3.sum() != 6)
        return 1;
    return 0;
}
