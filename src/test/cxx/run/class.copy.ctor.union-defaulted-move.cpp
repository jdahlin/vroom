// [class.copy.ctor]/10.1: a union with a non-trivial variant member has its
// defaulted move constructor defined as deleted (as in libc++ std::variant).

struct NonTrivial {
    int x;
    NonTrivial(int v) : x(v) {}
    NonTrivial(const NonTrivial &o) : x(o.x) {}
    NonTrivial(NonTrivial &&o) : x(o.x) { o.x = 0; }
    ~NonTrivial() {}
};

union VariantStorage {
    int a;
    NonTrivial b;

    VariantStorage(int v) : a(v) {}
    VariantStorage(const VariantStorage &) = default;
    VariantStorage(VariantStorage &&) = default;
    ~VariantStorage() {}
};

int main()
{
    NonTrivial nt(7);
    if (nt.x != 7)
        return 1;
    VariantStorage s(42);
    if (s.a != 42)
        return 2;
    return 0;
}
