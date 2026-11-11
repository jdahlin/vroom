#include "types.h"
int main(void)
{
    struct item a = {3, 1}, b = {7, 0};
    struct item r = item_change(a, 5, b);
    if (r.a != 15 || r.b) return 1;
    if (item_varargs(3, a, b, a) != 15) return 2;
    return 0;
}
