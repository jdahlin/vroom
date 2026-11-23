// new and delete: [expr.new], [expr.delete], [basic.stc.dynamic],
// [class.free]; the global functions replaced ([replacement.functions]),
// whose calls are not counted: an implementation may omit them
// ([expr.new]/12), as clang -O2 does
#include <stdio.h>
#include <stdlib.h>

typedef unsigned long size_t;

void *operator new(size_t n)
{
    return malloc(n);
}
void *operator new[](size_t n)
{
    return malloc(n);
}
void operator delete(void *p)
{
    free(p);
}
void operator delete(void *p, size_t)
{
    free(p);
}
void operator delete[](void *p)
{
    free(p);
}

// a placement allocation function ([expr.new]/17)
void *operator new(size_t, void *where) { return where; }

static int destroyed;

struct Counter {
    int value;
    Counter(int v) : value(v) {}
    ~Counter() { destroyed += value; }
};

struct Plain {
    int a, b;
};

struct WithDefault {
    int first = 6;
    int second;
};

template <class T, class... Args> T *make_new(Args... args)
{
    return new T(args...);
}

// class-specific allocation and deallocation ([class.free])
static char arena[64];
static int arena_used, arena_freed;
struct Pooled {
    long x;
    void *operator new(size_t n)
    {
        arena_used += n;
        return arena;
    }
    void operator delete(void *) { arena_freed++; }
};

int count(int n) { return n; }

int main()
{
    int failures = 0;
    int *p = new int(3);
    failures += *p != 3;
    delete p;

    int n = count(5);
    int *q = new int[n]; // [expr.new]/9: n * sizeof(int) bytes
    for (int i = 0; i < n; i++)
        q[i] = i;
    failures += q[4] != 4;
    delete[] q;

    Counter *c = new Counter(7);
    failures += c->value != 7;
    delete c; // the destructor, then operator delete(void*, size_t)
    failures += destroyed != 7;

    WithDefault *zeroed = make_new<WithDefault>();
    failures += zeroed->first != 6 || zeroed->second != 0;
    delete zeroed;
    Counter *from_pack = make_new<Counter>(3);
    failures += from_pack->value != 3;
    delete from_pack;

    Plain *x = new Plain{1, 2};
    failures += x->a + x->b != 3;
    delete x;
    Plain *y = new Plain[3];
    y[2].b = 9;
    failures += y[2].b != 9;
    delete[] y;

    const int *k = new const int(4);
    failures += *k != 4;
    delete k;
    int *z = new int(); // value-initialized: zero
    failures += *z != 0;
    delete z;
    double *d = new double{2.5};
    failures += *d != 2.5;
    delete d;
    int **pp = new int *(p);
    failures += *pp != p;
    delete pp;

    // placement: no allocation, the constructor in the buffer
    long buffer[2];
    Counter *t = new (buffer) Counter(11);
    void *at = t, *start = buffer;
    failures += t->value != 11 || at != start;

    Pooled *o = new Pooled;
    void *pooled = o, *pool = arena;
    failures += pooled != pool || arena_used != 8;
    delete o;
    failures += arena_freed != 1;
    // ::new and ::delete skip the class's functions ([expr.new]/11)
    Pooled *g = ::new Pooled;
    g->x = 1;
    ::delete g;
    failures += arena_freed != 1;

    int *null = 0;
    delete null; // nothing for a null pointer ([expr.delete]/7)

    printf("destroyed %d failures %d\n", destroyed, failures);
    return failures;
}
