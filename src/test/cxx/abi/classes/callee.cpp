#include "shared.h"

int Counted::live;
int Counted::copies;

Counted::Counted(int id) : id(id)
{
    live++;
}

Counted::Counted(const Counted &other) : id(other.id)
{
    live++;
    copies++;
}

Counted::~Counted()
{
    live--;
}

NonPod::NonPod(int i, char c) : i(i), c(c)
{
}

Derived::Derived(int i, char c, char d) : NonPod(i, c), d(d)
{
}

int Member::get() const
{
    return v;
}

void Member::set(int to)
{
    v = to;
}

int Member::twice(int n)
{
    return 2 * n;
}

namespace space
{
int scale = 7;

int scaled(int n)
{
    return n * scale;
}
} // namespace space

static int seven()
{
    return space::scale;
}

/* dynamic initialization ([basic.start.dynamic]) */
int initialized_first = seven() + 1;

Pod pod_swap(Pod p)
{
    Pod r;
    r.c = static_cast<char>(p.i);
    r.i = p.c;
    return r;
}

int derived_sum(const Derived &d)
{
    return d.i + d.c + d.d;
}

Derived derived_make(int i, char c, char d)
{
    return Derived(i, c, d);
}

int with_empty(WithEmpty w, Empty e, int after)
{
    (void)e;
    return w.x * 100 + after;
}

int bits_sum(Bits b)
{
    return static_cast<int>(b.a) + b.b + static_cast<int>(b.c) + b.d;
}

Bits bits_make(unsigned a, int b, unsigned c, char d)
{
    Bits r;
    r.a = a;
    r.b = b;
    r.c = c;
    r.d = d;
    return r;
}

Either either_of(int i)
{
    Either e;
    e.i = i;
    return e;
}

int counted_take(Counted c)
{
    return c.id * 10 + Counted::live;
}

Counted counted_make(int id)
{
    return Counted(id);
}

int call_back(int id)
{
    Counted c(id);
    Derived d(id, 2, 3);
    Bits b = bits_make(5, -9, 300, 4);
    return called_back(c, d, b);
}
