/* diag-flags: -std=c23 */
[[]];
int *[[]] const pointer;
_Static_assert(!__builtin_types_compatible_p(void(), void(int)), "prototype");
_Static_assert(__builtin_types_compatible_p(__typeof__(u8"abc"), unsigned char[4]), "u8");
void f(int n) {
    _Static_assert(__builtin_types_compatible_p(int[2][n], int[2][n]), "same");
    _Static_assert(!__builtin_types_compatible_p(int[4][n], int[5][n]), "bound");
    _Static_assert(__builtin_types_compatible_p(int[n], int[4]), "variable");
}
_Static_assert(U'\xffffffff' == 0xffffffffU, "wide high bit");
