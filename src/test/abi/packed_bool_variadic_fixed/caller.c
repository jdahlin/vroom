/* vcc-xfail-win64: Windows x64: packed boolean vectors at calls (TODO.md) */
#include "types.h"
int main(void)
{
    B b = {}; b[64] = 1;
    B r = fixed(b, 19);
    if (!r[0] || !r[64]) return 1;
    B (*volatile indirect)(B, ...) = fixed;
    r = indirect(b, 19);
    return !r[0] || !r[64];
}
