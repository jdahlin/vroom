// Calling a member function of a private base class made public via a
// using-declaration ([class.access.base]/5, [namespace.udecl]/15).

struct Base {
    int n;
    int size() const { return n; }
};

class Derived : private Base {
public:
    Derived(int v) : Base{v} {}
    using Base::size;
};

class Holder {
    Derived d;
public:
    Holder(int v) : d(v) {}
    int get() const { return d.size(); }
};

int main() {
    Holder h(42);
    return h.get() == 42 ? 0 : 1;
}
