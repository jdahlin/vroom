// An empty base class without constructors initialized from the derived
// object in a mem-initializer (nlohmann::basic_json's copy and move
// constructors: json_base_class_t(other)): no byte is copied, the
// expression is evaluated.
extern "C" int printf(const char *, ...);
struct base {};
template <class T> T &&fwd(T &t) { return static_cast<T &&>(t); }
int calls = 0;
struct J : base {
    int v;
    J(int x) : v(x) {}
    J(const J &o) : base(o), v(o.v + 1) {}
    J(J &&o) : base(fwd<base>(o)), v(o.v + 2) {}
    const J &self() const { calls++; return *this; }
};
struct K : base {
    int w;
    K(const J &j) : base(j.self()), w(j.v) {}
};
int main() {
    J a(1);
    J b(a);
    J c(fwd(b));
    K k(c);
    printf("%d %d %d %d %d\n", a.v, b.v, c.v, k.w, calls);
    return c.v + k.w + calls - 9;
}
