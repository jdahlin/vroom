#include <stdio.h>

#include "shared.h"

int main()
{
    Empty e;
    Empty2 f;
    printf("empty %d\n", empty_mid(1, e, 2, f, 3));
    Empty r = empty_ret(4);
    Empty2 r2 = empty2_ret(r);
    printf("empty sizes %d %d\n", static_cast<int>(sizeof(r)),
           static_cast<int>(sizeof(r2)));

    XYZW v = hfa_make(1.5f, 2.5f, 3.5f, 4.5f);
    printf("hfa %g %g %g %g sum %g\n", v.x, v.y, v.z, v.w, hfa_sum(v, 2.0f));
    D3 d = hfa_d3_make(1.25);
    printf("d3 %g %g %g sum %g\n", d.a, d.b, d.c, hfa_d3(d, 9));
    Mixed m = mixed_make(0.5f, 8.25);
    printf("mixed %g %g %g sum %g\n", m.x, m.y, m.d, mixed_sum(m));

    Copied a(1);
    Copied c(3);
    printf("copied %d\n", copied_take(a, 2, c));
    Copied made = copied_make(5);
    printf("copied made %d\n", made.v);

    {
        Destroyed x;
        x.v = 7;
        printf("destroyed take %d\n", destroyed_take(x));
        printf("destroyed count %d\n", Destroyed::destroyed);
        Destroyed y = destroyed_make(20);
        printf("destroyed made %d count %d\n", y.v, Destroyed::destroyed);
    }
    printf("destroyed count %d\n", Destroyed::destroyed);

    Dyn dy(4);
    printf("dyn %d\n", dyn_take(dy));
    Dyn dm = dyn_make(6);
    printf("dyn made %d %d\n", dm.v, dm.get());

    int i = 10;
    long l = 20;
    Ref rf = ref_make(i, l);
    printf("ref %d i %d\n", ref_sum(rf), i);

    BitsU b = bitsu_make(3, 1000, -5);
    printf("bitsu %u %u %d sum %d\n", b.a, b.b, b.u.i, bitsu_sum(b));

    Big g = big_make(11);
    printf("big %ld %ld %ld %d sum %ld a %ld\n", g.a, g.b, g.c, g.d, big_sum(g),
           g.a);

    Small s = {1, 2};
    printf("many %ld\n", many(s, s, s, s, s, s, s, s, s, g, v, d, a, s));
    return 0;
}
