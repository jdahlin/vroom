// [over.match.oper]: for a = b with b of the class type, the implicit copy
// assignment beats operator=(const S&) through a conversion function.
int other;
struct S {
    S() = default;
    S &operator=(const S &) = default;
};
struct O {
    int v = 0;
    O &operator=(const S &) { other++; return *this; }
    operator S() const { return S(); }
};
void f(O &a, const O &b) { a = b; }

int main() {
    O a, b;
    b.v = 7;
    f(a, b);
    return a.v != 7 || other != 0;
}
