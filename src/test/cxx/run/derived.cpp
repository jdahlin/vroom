// [class.derived]: non-virtual base classes: layout ([abi:layout], empty
// bases at offset 0), member lookup through bases, [conv.ptr]/3 and
// [dcl.init.ref] to a base, [class.access.base], [class.protected],
// [class.base.init] (base and member constructors, default ones), and
// [class.dtor]/13 (member and base destructors after the body)
extern "C" int printf(const char *, ...);

struct A {
    int a;
    int get() const { return a; }
};
struct B : A {
    int b;
    int sum() const { return a + b + get(); }
};
struct E {};
struct F : E {
    int f;
};
struct G : A, F {
    char g;
};

int events[32];
int count;
void note(int e) { events[count++] = e; }

struct C {
    int c;
    C() {
        c = 7;
        note(1);
    }
    C(int v) : c(v) { note(5); }
    ~C() { note(2); }
};
struct D : C {
    int d;
    D() : d(3) { note(3); }
    D(int v) : C(v * 2), d(v) { note(6); }
    ~D() {
        note(4);
        if (d == 9)
            return;
        note(10);
    }
};
struct M {
    C m;
    int x;
    M() : x(1) {}
    ~M() {}
};

class Shape {
  protected:
    int sides;

  public:
    void set(int s) { sides = s; }
};
class Square : public Shape {
  public:
    int twice() const { return sides * 2; }
};
class Hidden : private A {
  public:
    int peek() {
        a = 11;
        return get();
    }
};

int by_ref(const A &x) { return x.a; }
int by_ptr(const A *p) { return p ? p->a : -1; }
A copy_of(const B &b) {
    A a = b;
    return a;
}

int main() {
    int t = 0;
    B b;
    b.a = 1;
    b.b = 2;
    t += b.sum() + b.get() + b.a;
    F f;
    f.f = 5;
    t += f.f + sizeof(F) + sizeof(G) * 3;
    G g;
    g.a = 4;
    g.f = 6;
    g.g = 1;
    t += g.a + g.f + g.g + g.get();
    A *pa = &b;
    t += pa->a * 10 + by_ref(b) + by_ptr(&b) + by_ptr((B *)0);
    A &ra = b;
    ra.a = 20;
    t += b.a + copy_of(b).a;
    {
        D d;
        t += d.c + d.d;
    }
    {
        D d(9);
        t += d.c + d.d;
    }
    {
        M m;
        t += m.m.c + m.x;
    }
    Square sq;
    sq.set(4);
    t += sq.twice();
    Hidden h;
    t += h.peek();
    for (int i = 0; i < count; i++)
        t = (t * 3 + events[i]) % 100003;
    printf("%d %d\n", t, count);
    return 0;
}
