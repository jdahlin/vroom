#include "types.h"
int main(void)
{
    struct complex_float cf = cf_roundtrip((struct complex_float){ 1.0f + 4.0fi });
    struct doubles d = doubles_roundtrip((struct doubles){ 2, 3 });
    struct padded p = padded_roundtrip((struct padded){ 5, 7 });
    struct bits b = bits_roundtrip((struct bits){ 19, 1 });
    return __real__ cf.x != 3 || __imag__ cf.x != 7 || d.x != 7 || d.y != 10 ||
           p.x != 16 || p.y != 20 || b.x != 36 || b.b != 0;
}
