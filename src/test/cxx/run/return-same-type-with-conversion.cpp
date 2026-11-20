// [class.copy.elision]/3: returning a local of the returned type whose class
// has conversion functions: the copy or move constructor is chosen, not a
// converting constructor reached through the conversion function.
struct P {
    int v;
    constexpr P(int x) : v(x) {}
};
struct W {
    int v;
    constexpr W(int x) : v(x) {}
    constexpr operator P() const { return P(v + 100); }
};
W make(W a) {
    W w = a;
    return w;
}
W pass(W a) { return a; }
int main() {
    if (make(W(3)).v != 3)
        return 1;
    if (pass(W(4)).v != 4)
        return 2;
    return 0;
}
