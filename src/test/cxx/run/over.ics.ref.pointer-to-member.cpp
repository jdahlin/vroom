// [over.ics.ref]: a pointer to member function bound to a forwarding
// reference of a constructor template.
int called;
struct Th {
    template <class F, class... Args, int = 0>
    explicit Th(F &&f, Args &&...args) { called += sizeof...(args); }
};
struct T {
    void f() {}
    void g() { Th t(&T::f, this); }
};

int main() {
    T t;
    t.g();
    return called != 1;
}
