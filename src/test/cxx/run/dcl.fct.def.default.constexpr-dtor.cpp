// [dcl.fct.def.default]/3, [dcl.constexpr]/5: constexpr and non-public
// defaulted destructors.
struct Base {
    int x;
    constexpr Base(int v) : x(v) {}
protected:
    constexpr ~Base() = default;
};

struct Derived : Base {
    constexpr Derived(int v) : Base(v) {}
};

int main()
{
    Derived d(42);
    return d.x == 42 ? 0 : 1;
}
