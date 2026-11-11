/* vcc-xfail-wasm: TODO.md _BitInt widths above 128 and remaining targets: wasm runtime precision not implemented. */
/* vcc-flags: -std=c23 */
#define L(x,n) __builtin_stdc_rotate_left(x,n)
#define R(x,n) __builtin_stdc_rotate_right(x,n)
int main(void) {
    volatile unsigned _BitInt(7) narrow = 65;
    if (L(narrow, 1) != 3 || R(narrow, 1) != 96) return 1;
    volatile unsigned _BitInt(1) one = 1;
    if (L(1u, one) != 2) return 2;
    return 0;
}
