/* vcc-xfail: doc/PLAN-cxx-sema.md S4: a class whose layout is not C's (a base's tail padding reused) is not lowered */
#include <stdio.h>

#include "shared.h"

int main()
{
    printf("sizes %d %d %d\n", static_cast<int>(sizeof(Base)),
           static_cast<int>(sizeof(Derived)), static_cast<int>(sizeof(Twice)));
    Twice t = make(7);
    printf("made %d %d %d %d sum %d\n", t.i, t.c, t.d, t.e, sum(t));
    fill(&t, 40, 5);
    printf("filled %d %d %d %d sum %d\n", t.i, t.c, t.d, t.e, sum(t));
    Derived d(1, 2, 3);
    Derived s = step(d);
    printf("step %d %d %d\n", s.i, s.c, s.d);
    return 0;
}
