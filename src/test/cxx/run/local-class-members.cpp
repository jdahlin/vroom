/* [class.local]: member functions and constructors of local classes, and
   of a class nested in one: <local-name> symbols (Z <function> E ...) */
struct Base {
    int v;
};

static int apply(int x)
{
    struct Twice {
        int k;
        Twice(int a) : k(a) {}
        int run(int y) const { return k * y * 2; }
        struct Inner {
            int get() const { return 5; }
        };
        int same(const Twice *other) const { return other->k == k; }
    };
    Twice t(x);
    Twice::Inner i;
    return t.run(3) + i.get() + t.same(&t);
}

int main()
{
    struct Counter {
        int n = 0;
        void bump() { n++; }
    };
    Counter c;
    c.bump();
    c.bump();
    return apply(2) + c.n - 20;
}
