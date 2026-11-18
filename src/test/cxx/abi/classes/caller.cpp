#include <stdio.h>

#include "shared.h"

int called_back(Counted c, Derived d, Bits b)
{
    return c.id + derived_sum(d) + bits_sum(b) + Counted::live * 1000;
}

int main()
{
    printf("sizes %d %d %d %d %d %d %d %d\n", static_cast<int>(sizeof(Empty)),
           static_cast<int>(sizeof(Pod)), static_cast<int>(sizeof(NonPod)),
           static_cast<int>(sizeof(Derived)),
           static_cast<int>(sizeof(WithEmpty)), static_cast<int>(sizeof(Bits)),
           static_cast<int>(sizeof(Either)), static_cast<int>(sizeof(Counted)));
    printf("initialized %d\n", initialized_first);

    Pod p;
    p.c = 3;
    p.i = 65;
    Pod q = pod_swap(p);
    printf("pod %d %d\n", q.c, q.i);

    Derived d = derived_make(100, 20, 3);
    printf("derived %d %d %d sum %d\n", d.i, d.c, d.d, derived_sum(d));

    WithEmpty w;
    w.x = 12;
    Empty e;
    printf("empty %d\n", with_empty(w, e, 34));

    Bits b = bits_make(5, -9, 300, 4);
    printf("bits %d %d %d %d sum %d\n", static_cast<int>(b.a), b.b,
           static_cast<int>(b.c), b.d, bits_sum(b));

    Either u = either_of(0x01020304);
    printf("either %d %d\n", u.i, u.c[0]);

    {
        Counted c(4);
        printf("take %d\n", counted_take(c));
        printf("live %d copies %d\n", Counted::live, Counted::copies);
        Counted made = counted_make(9);
        printf("made %d live %d copies %d\n", made.id, Counted::live,
               Counted::copies);
        printf("back %d\n", call_back(6));
    }
    printf("live %d copies %d\n", Counted::live, Counted::copies);

    Member m;
    m.set(21);
    printf("member %d %d\n", m.get(), Member::twice(m.v));
    printf("space %d %d\n", space::scaled(6), space::scale);
    return 0;
}
