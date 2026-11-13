/* vcc-flags: -std=c23 */
#include <stdatomic.h>
#include <stdlib.h>
_Static_assert(__builtin_types_compatible_p(atomic_char8_t, _Atomic(unsigned char)), "atomic UTF-8 code unit");
_Static_assert(ATOMIC_CHAR8_T_LOCK_FREE == __CLANG_ATOMIC_CHAR8_T_LOCK_FREE, "char8 lock freedom");
int main(void)
{
    atomic_char8_t value = 255;
    if (value++ != 255 || value != 0 || ++value != 1) abort();
    return 0;
}
