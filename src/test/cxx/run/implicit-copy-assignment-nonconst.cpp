// [class.copy.assign]/2: the implicit copy assignment operator is
// X& operator=(X&) when a member's copy assignment takes a non-const
// reference.
struct Member {
    int v = 0;
    Member() = default;
    Member &operator=(Member &other) {
        v = other.v + 100;
        return *this;
    }
};
struct Holder {
    Member m;
    int n = 0;
};
struct Derived : Member {
    int k = 0;
};

int main() {
    Holder a, b;
    a.m.v = 1;
    a.n = 2;
    b = a;
    Derived d, e;
    d.v = 5;
    d.k = 6;
    e = d;
    return b.m.v != 101 || b.n != 2 || e.v != 105 || e.k != 6;
}
