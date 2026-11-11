#include "types.h"
int main(void)
{
    struct empty e;
    struct float_empty f = fempty((struct float_empty){ .value = 2 });
    struct ld_empty l = ldempty((struct ld_empty){ .value = 3 });
    union double_empty d = dempty((union double_empty){ .value = 5 });
    if (f.value != 5 || l.value != 8 || d.value != 12) return 1;
    struct packed_hfa p = packed_empty((struct packed_hfa){ .a=1, .b=2, .c=3, .d=4 });
    if (p.a != 2 || p.b != 4 || p.c != 6 || p.d != 8) return 3;
    p = packed_empty_stack(1,2,3,4,5,6,7,8,p);
    if (p.a != 5 || p.b != 11 || p.c != 17 || p.d != 23) return 4;
    if (empty_after_stack(1,2,3,4,5,6,7,8,101LL,e,102LL,e,103LL)) return 5;
    return 2 * empty_va(9, e, 11LL, e, 12LL, e, 13LL, e, 14LL, e, 15LL,
                    e, 16LL, e, 17LL, e, 18LL, e, 19LL);
}
