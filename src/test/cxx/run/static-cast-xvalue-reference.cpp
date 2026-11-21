// [expr.static.cast]/4: static_cast<const T&> of an xvalue.
struct S { int v; };

int main() {
    S s{3};
    int n = 7;
    const S &r = static_cast<const S &>(static_cast<S &&>(s));
    const int &i = static_cast<const int &>(static_cast<int &&>(n));
    return r.v != 3 || i != 7;
}
