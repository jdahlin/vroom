// [dcl.spec.auto]/2: in-class non-virtual member function with deduced
// auto return type.
struct Counter {
    int n;
    auto value() const { return n + 1; }
    static auto doubled(int x) { return x * 2; }
};

int main() {
    Counter c{4};
    return c.value() != 5 || Counter::doubled(3) != 6;
}
