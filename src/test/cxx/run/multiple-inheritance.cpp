// [class.mi], [class.virtual]: more than one dynamic base: the secondary
// virtual tables ([abi:vtable-general]) with their offsets to top, a
// derived class's overriders called through a secondary base by
// non-virtual thunks ([abi:thunks]), conversions to a base at a nonzero
// offset (a null pointer stays null, [conv.ptr]/3), virtual calls made
// in a constructor of a class with two dynamic bases ([class.cdtor]/4)
extern "C" int printf(const char *, ...);
struct Shape {
    Shape(int i) : id(i) {}
    virtual int area() const { return 0; }
    virtual int scaled(int by) const { return area() * by + id; }
    int id;
};
struct Named {
    Named() : tag(7) { printf("Named() %s\n", name()); }
    virtual const char *name() const { return "named"; }
    virtual int weight() const { return tag; }
    int tag;
};
struct Both : Shape, Named {
    Both(int s) : Shape(9), side(s) { printf("Both() %s %d\n", name(), area()); }
    int area() const override { return side * side; }
    const char *name() const override { return "both"; }
    int side;
};
struct More : Both {
    More() : Both(2) {}
    int weight() const override { return 100 + side; }
};
// a class whose two bases each have one virtual function, overriding both
struct L {
    virtual int f(int x) { return x + 1; }
    long l;
};
struct R {
    virtual int g(int x) { return x + 2; }
    long r;
};
struct LR : L, R {
    int f(int x) override { return x * 10; }
    int g(int x) override { return x * 20 + static_cast<int>(r); }
};
const Named *as_named(const Both *b) { return b; }
int main()
{
    Both b(4);
    const Shape &s = b;
    const Named &n = b;
    printf("%d %d %s %d\n", s.area(), s.scaled(2), n.name(), n.weight());
    More m;
    const Named *mn = &m;
    printf("%s %d %d\n", mn->name(), mn->weight(), m.scaled(3));
    printf("null %d\n", as_named(nullptr) == nullptr);
    printf("same %d\n", as_named(&b) == &n);
    LR lr;
    lr.l = 1;
    lr.r = 5;
    L *pl = &lr;
    R *pr = &lr;
    printf("%d %d %d\n", pl->f(3), pr->g(3), lr.g(1));
    printf("sizes %d %d %d\n", static_cast<int>(sizeof(Both)),
           static_cast<int>(sizeof(More)), static_cast<int>(sizeof(LR)));
    return 0;
}
