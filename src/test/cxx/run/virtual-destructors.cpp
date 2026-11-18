// virtual destructors: [class.dtor]/13 (a destructor overriding a base's
// virtual one, the implicit one virtual too), [expr.delete]/3 (delete
// through a pointer to a base), [class.dtor]/12 (the deleting destructor's
// deallocation function, class-specific or global) and [class.cdtor]/4 (a
// virtual call in a destructor finds the destructor's class's overrider);
// [abi:vtable-components]: the D1/D0 pair of slots
#include <stdio.h>
#include <stdlib.h>

typedef unsigned long size_t;

static int allocated, freed;
void *operator new(size_t n)
{
    allocated++;
    return malloc(n);
}
void operator delete(void *p)
{
    freed++;
    free(p);
}
void operator delete(void *p, size_t)
{
    freed++;
    free(p);
}

static int trace[16];
static int traced;
static void note(int what) { trace[traced++] = what; }

struct Base {
    int id;
    Base() : id(1) {}
    virtual ~Base() { note(10 + who()); }
    virtual int who() { return 1; }
};

struct Derived : Base {
    int extra;
    Derived() : extra(5) {}
    ~Derived() { note(20 + who()); }
    int who() { return 2; }
};

// no user-declared destructor: the implicit one is virtual ([class.dtor]/13)
struct Implicit : Derived {
    int more;
    int who() { return 3; }
};

// a destructor defined out of the class: the key function
struct Keyed {
    int k;
    virtual ~Keyed();
    virtual int value() { return 4; }
};
Keyed::~Keyed() { note(40); }
struct FromKeyed : Keyed {
    int value() { return 5; }
    ~FromKeyed() { note(50); }
};

// class-specific deallocation, found by the deleting destructor
static int pool_freed;
struct Pooled {
    long x;
    virtual ~Pooled() {}
    void operator delete(void *p)
    {
        pool_freed++;
        free(p);
    }
};
struct PooledDerived : Pooled {
    long y;
    ~PooledDerived() { note(60); }
};

// a defaulted virtual destructor, and one defaulted in a derived class
struct Defaulted {
    int d;
    virtual ~Defaulted() = default;
};
struct DefaultedDerived : Defaulted {
    Derived member; // its destructor runs from the implicit one
    ~DefaultedDerived() = default;
};

// a dynamic class whose destructor is not virtual: it still sees its own
// overriders ([class.cdtor]/4)
struct Plain {
    int p;
    virtual int f() { return 1; }
    ~Plain() { note(70 + f()); }
};
struct PlainDerived : Plain {
    int f() { return 2; }
    ~PlainDerived() { note(80 + f()); }
};

static void destroy(Base *b) { delete b; }

int main()
{
    int failures = 0;
    Base *b = new Derived;
    failures += b->who() != 2;
    destroy(b); // ~Derived, then ~Base: who() is Base's there
    failures += traced != 2 || trace[0] != 22 || trace[1] != 11;

    traced = 0;
    b = new Implicit;
    failures += b->who() != 3;
    delete b;
    failures += traced != 2 || trace[0] != 22 || trace[1] != 11;

    traced = 0;
    Keyed *k = new FromKeyed;
    failures += k->value() != 5;
    delete k;
    failures += traced != 2 || trace[0] != 50 || trace[1] != 40;

    traced = 0;
    Pooled *p = new PooledDerived;
    delete p;
    failures += pool_freed != 1 || traced != 1 || trace[0] != 60;

    {
        traced = 0;
        Derived d; // destroyed directly at the end of the block
        failures += d.who() != 2;
    }
    failures += traced != 2 || trace[0] != 22 || trace[1] != 11;

    traced = 0;
    Defaulted *q = new DefaultedDerived;
    delete q;
    failures += traced != 2 || trace[0] != 22 || trace[1] != 11;

    traced = 0;
    {
        PlainDerived pd;
        failures += pd.f() != 2;
    }
    failures += traced != 2 || trace[0] != 82 || trace[1] != 71;

    Base *none = 0;
    delete none; // nothing for a null pointer ([expr.delete]/6)
    failures += allocated != freed + 1;
    printf("virtual destructors: %d failures\n", failures);
    return failures;
}
