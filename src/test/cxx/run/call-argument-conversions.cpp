// [over.call.object]/2: only conversions of the called object to function
// pointer or reference types contribute surrogate call candidates.
struct Argument {
    operator int() const { return 9; }
};
struct Callable {
    explicit operator bool() const { return true; }
    int operator()(Argument) const { return 9; }
};
using Fn = int (*)(int);
struct ExplicitPointer {
    explicit operator Fn() const { return nullptr; }
    int operator()(int n) const { return n; }
};
struct Base {
    operator int() const { return 3; }
};
struct Derived : Base {
    int operator()(int n) const { return n; }
};
int main() {
    Callable f;
    Derived d;
    ExplicitPointer p;
    return f(Argument{}) != 9 || d(5) != 5 || p(7) != 7;
}
