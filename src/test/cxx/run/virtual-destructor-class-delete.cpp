// [class.dtor]/12, [basic.def.odr]/8: the deallocation function chosen for
// a virtual destructor is odr-used where the destructor is defined (the
// deleting destructor calls it), so a class template's member operator
// delete is instantiated ([temp.inst]/4) though no delete-expression names
// it: here the base's constructor is emitted, and with it the virtual
// table, while nothing deletes a G.
#include <stdio.h>

typedef decltype(sizeof 0) size_t;

template <class T> struct Pool {
    template <Pool<T> *P> struct Item {
        void operator delete(void *, size_t size)
        {
            printf("delete %d\n", (int)size);
        }
    };
};

struct G;
Pool<G> pool;

struct G : Pool<G>::Item<&pool> {
protected:
    G() {}
    virtual int f() const = 0;

public:
    virtual ~G() = default;
    static int use(const G *g) { return g->f(); }
};

struct H : G {
    H() : G() {}
    int f() const override { return 1; }
};

int main()
{
    puts("main");
    return 0;
}
