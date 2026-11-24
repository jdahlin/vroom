struct F {
    virtual int operator()(int) const = 0;
    virtual ~F() {}
    virtual F& operator+=(int) { return *this; }
    virtual int operator-() const { return 0; }
};
struct G : F {
    int operator()(int x) const override { return x + 1; }
};
struct H : G {
    int operator-() const override { return -7; }
    int operator()(int x) const override { return x * 2; }
};
int main() {
    G g;
    H h;
    const F& f = g;
    const F& k = h;
    F* p = &h;
    if (f(3) != 4) return 1;
    if (k(3) != 6) return 2;
    if ((*p)(5) != 10) return 3;
    if (p->operator()(5) != 10) return 4;
    if (g(1) != 2) return 5;
    if (-*p != -7) return 6;
    if (-g != 0) return 7;
    return 0;
}
