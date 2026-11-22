// [class.derived], [class.nest], [class.local]: a nested class and a local
// class with a base-clause, the nested one overriding a virtual function.
struct B {
    int b = 1;
    virtual int f() { return 10; }
    virtual ~B() {}
};

struct Outer {
    struct Inner : B {
        int n = 2;
        int f() override { return 20 + n; }
    };
    struct Deeper {
        struct Leaf : Inner {
            int g() { return b + n + f(); }
        };
    };
    Inner m;
};

int main()
{
    struct Local : B {
        int l = 3;
        int get() const { return 30 + l; }
    };
    Outer o;
    Local x;
    Outer::Deeper::Leaf leaf;
    B &r = x;
    if (o.m.f() != 22)
        return 1;
    if (r.f() != 10 || x.get() != 33 || x.b != 1)
        return 2;
    if (leaf.g() != 1 + 2 + 22)
        return 3;
    if (sizeof(Local) < sizeof(B) + sizeof(int))
        return 4;
    return 0;
}
