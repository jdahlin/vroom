// [class.conv.fct], [conv.rval]: a conversion function called on an
// aggregate prvalue temporary (T{...} converted to another type).
struct A {
    int x;
    operator int() const { return x + 1; }
};
struct B {
    int x, y;
    operator long() { return x * 10 + y; }
    explicit operator bool() const { return x != 0; }
};

int main() {
    int r = A{4};
    long l = B{3, 2};
    bool t = static_cast<bool>(B{1, 0});
    bool f = static_cast<bool>(B{});
    return r != 5 || l != 32 || !t || f;
}
