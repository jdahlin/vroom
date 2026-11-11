#include "classes.h"
int main(void)
{
    v1ll v = { 0x1234567812345678LL };
    if (vector_identity(v)[0] != v[0]) return 1;
    if (ld_identity((struct ld){1.25L}).x != 1.75L) return 2;
    struct padded a = {2}, b = {3};
    if (padded_sum(a, 7, b).x != 12) return 3;
    if (padded_va(5, a, b, a, b, a) != 12) return 4;
    if (odd_identity((struct odd){2, 1234}).b != 1236) return 5;
#ifdef __x86_64__
    v16f wide = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    v16f got = wide_identity(wide);
    for (int i=0;i<16;i++) if (got[i] != wide[i]) return 6;
#endif
    struct under u = under_sum((struct under){1,2,3}, 7, (struct under){4,5,6});
    if (u.a != 5 || u.b != 7 || u.c != 16) return 7;
    return 0;
}
