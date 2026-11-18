#include "shared.h"

Base::Base(int i, char c) : i(i), c(c)
{
}

Derived::Derived(int i, char c, char d) : Base(i, c), d(d)
{
}

Twice::Twice(int i, char c, char d, char e) : Derived(i, c, d), e(e)
{
}

int sum(const Twice &t)
{
    return t.i + t.c * 10 + t.d * 100 + t.e * 1000;
}

Twice make(int i)
{
    return Twice(i, 1, 2, 3);
}

Derived step(Derived d)
{
    d.i++;
    d.c++;
    d.d++;
    return d;
}

/* writes the base's members only: the derived class's are in what would
   be the base's padding */
void fill(Base *b, int i, char c)
{
    b->i = i;
    b->c = c;
}
