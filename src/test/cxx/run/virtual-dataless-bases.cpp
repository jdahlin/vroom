/* [abi:definitions]: a dynamic class is never empty (its virtual table pointer
   is its data), so a base with virtual functions or virtual bases and no
   data members is a subobject with bytes: dispatch through it, a derived
   class adding nothing, virtual destructors, and a diamond of virtual
   bases with no data in the middle. */
extern "C" int printf(const char *, ...);

struct Shape {
    virtual int sides() const { return 0; }
    virtual ~Shape() { printf("~Shape\n"); }
};
struct Square : Shape {
    int sides() const override { return 4; }
    ~Square() { printf("~Square\n"); }
};
struct Plain : Square {
};

struct A {
    int a = 1;
};
struct L : virtual A {
};
struct R : virtual A {
};
struct M : L, R {
    int m = 2;
};

int count(const Shape &s) { return s.sides(); }

int main()
{
    Shape s;
    Square q;
    Plain p;
    Shape *all[3] = {&s, &q, &p};
    int total = 0;
    for (Shape *x : all)
        total += count(*x);
    Shape *h = new Square;
    total += h->sides();
    delete h;
    M m;
    L &l = m;
    R &r = m;
    total += l.a + r.a + m.m;
    printf("%d %zu %zu\n", total, sizeof(Plain), sizeof(M));
    return total - 16;
}
