// [class.dtor]/13: the implicit destructor of a class whose base's is
// virtual is declared when the class's virtual table is made, an entity
// added while the class's is in use.  Enough classes that the entity table
// grows at one of them (a use after free under ASan before the fix).
#include <stdio.h>
#include <stdlib.h>

typedef unsigned long size_t;
/* the deleting destructors' */
void operator delete(void *p) { free(p); }
void operator delete(void *p, size_t) { free(p); }

static int destroyed;

struct Base {
    int b;
    Base() : b(1) {}
    virtual ~Base() { destroyed++; }
    virtual int f() { return 1; }
};

#define D1(n) \
    struct D##n : Base { \
        int x; \
    };
#define D10(n) D1(n##0) D1(n##1) D1(n##2) D1(n##3) D1(n##4) D1(n##5) D1(n##6) D1(n##7) D1(n##8) D1(n##9)
#define D100(n) D10(n##0) D10(n##1) D10(n##2) D10(n##3) D10(n##4) D10(n##5) D10(n##6) D10(n##7) D10(n##8) D10(n##9)
D100(1)
D100(2)
D100(3)
D100(4)
D100(5)
D100(6)
D100(7)

struct Last : Base {
    int f() { return 7; }
};

int main()
{
    {
        D100 d;
        D555 e;
        Last l;
        Base &r = l;
        printf("%d %d %d\n", d.f(), e.f(), r.f());
    }
    printf("%d\n", destroyed);
    return 0;
}
