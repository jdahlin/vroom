// [dcl.init.ref]/5.3.2, [class.temporary]/6: an aggregate prvalue bound to
// a reference to its base class or to a base's member function's object.
struct Base { int b; int getb() const { return b; } };
struct Derived : Base { int d; };
struct Wide : Base { int w; ~Wide() {} };

int take(const Base &x) { return x.b; }

int main() {
    const Base &r = Derived{{3}, 4};
    Base &&q = Derived{{5}, 6};
    const Base &w = Wide{{9}, 2};
    return r.b != 3 || q.b != 5 || w.b != 9 || take(Derived{{8}, 1}) != 8;
}
