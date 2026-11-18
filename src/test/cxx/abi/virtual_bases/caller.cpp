#include <stdio.h>

#include "shared.h"

D::D(int d) : A(d * 2), B(d + 1), C(d + 2), d(d)
{
    trace[traced++] = 4;
}

D::~D()
{
    trace[traced++] = -4;
}

int main()
{
    {
        D d(5);
        const A *a = &d;
        const C *c = &d;
        printf("d %d %d %d %d %d\n", value_of(a), b_of(d), c_of(c),
               a_through_c(c), d.a);
        printf("b %d %d\n", b_of(*callee_b()), value_of(callee_b()));
        printf("sizes %d %d %d %d\n", static_cast<int>(sizeof(A)),
               static_cast<int>(sizeof(B)), static_cast<int>(sizeof(C)),
               static_cast<int>(sizeof(D)));
    }
    for (int i = 0; i < traced; i++)
        printf("%d ", trace[i]);
    printf("\n");
    return 0;
}
