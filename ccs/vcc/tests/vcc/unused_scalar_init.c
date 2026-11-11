/* Dropping unused storage must preserve initializer evaluation and exclusions. */
#define NI __attribute__((noinline))
static volatile int ui_seen, ui_input;
static int ui_cleaned;
static volatile _Atomic unsigned ui_atom;
NI int ui_touch(int x) { ui_seen += x; return x; }
NI void ui_clean(int *p) { ui_cleaned += *p; }
NI int ui_pure(int x) { int dead = x * 13 + 7; return 17; }
NI int ui_unsigned(unsigned x) { unsigned dead = (x * 13u + 7u) ^ 19u; return 17; }
NI int ui_conditional(int x, int y) { int dead = x ? y * 13 + 7 : y * 17 + 9; return 17; }
NI int ui_local_read(int x) { int source = x + 3; int dead = source * 13 + 7; return 17; }
NI int ui_logic(int x, int y) { int dead = (x && y) || (x < y); return 17; }
NI int ui_call(int x) { int dead = ui_touch(x); return 17; }
NI int ui_comma(int x) { int dead = (ui_touch(x), x * 13 + 7); return 17; }
NI int ui_short_effect(int x) { int dead = x && ui_touch(3); return 17; }
NI int ui_volatile_read(void) { int dead = ui_input * 13 + 7; return 17; }
NI int ui_volatile_destination(int x) { volatile int dead = x * 13 + 7; return 17; }
NI int ui_atomic_read(void) { unsigned dead = ui_atom; return 17; }
NI int ui_atomic_effect(void) { unsigned dead = __c11_atomic_fetch_add(&ui_atom, 3, 5); return 17; }
NI int ui_atomic_destination(int x) { _Atomic int dead = x * 13 + 7; return 17; }
NI int ui_div(int x, int divisor) { int dead = x / divisor; return 17; }
NI int ui_shift(unsigned x, unsigned count) { unsigned dead = x << count; return 17; }
NI int ui_load(const int *p) { int dead = *p; return 17; }
NI int ui_float_conversion(double x, double y) { int dead = (int)(x / y); return 17; }
NI int ui_fp_destination(double x) { double dead = x * 3.0 + 7.0; return 17; }
NI int ui_cleanup(int x) { int dead __attribute__((cleanup(ui_clean))) = ui_touch(x); return 17; }
NI int ui_braced(int x) { int dead = { ui_touch(x) }; return 17; }
NI int ui_zero(void) { int dead = { 0 }; return 17; }
NI int ui_vla(int x) { int dead[ui_touch(x)]; return 17; }
NI int ui_static(void) { static int dead __attribute__((used)) = 19; return 17; }
NI int ui_used(int x) { int dead __attribute__((used)) = x * 13 + 7; return 17; }
NI void ui_observe(int *p) { ui_seen += *p; }
NI int ui_escape(int x) { int dead = x * 13 + 7; ui_observe(&dead); return 17; }
__attribute__((noinline, optnone)) int ui_noopt(int x) { int dead = x * 13 + 7; return 17; }
static inline int ui_inline_body(int x) { int dead = x * 13 + 7; return x + 3; }
NI int ui_inline_a(int x) { return ui_inline_body(x); }
NI int ui_inline_b(int x) { return ui_inline_body(x) + ui_inline_body(x + 1); }
#if defined(__x86_64__) || defined(__aarch64__)
NI int ui_fixed(int x)
{
#if defined(__x86_64__)
    register int dead __asm__("r12") = x * 13 + 7;
#else
    register int dead __asm__("x19") = x * 13 + 7;
#endif
    return 17;
}
#endif
int main(void)
{
    if (ui_pure(3) != 17 || ui_unsigned(0xffffffffU) != 17) return 1;
    if (ui_conditional(0, 3) != 17 || ui_conditional(1, 3) != 17 || ui_local_read(3) != 17 || ui_logic(3, 7) != 17) return 2;
    if (ui_call(3) != 17 || ui_seen != 3 || ui_comma(5) != 17 || ui_seen != 8) return 3;
    if (ui_short_effect(0) != 17 || ui_seen != 8 || ui_short_effect(1) != 17 || ui_seen != 11) return 4;
    ui_input = 37;
    if (ui_volatile_read() != 17 || ui_volatile_destination(3) != 17 || ui_input != 37) return 5;
    ui_atom = 7;
    if (ui_atomic_read() != 17 || ui_atomic_effect() != 17 || ui_atom != 10) return 6;
    if (ui_atomic_destination(3) != 17 || ui_shift(3, 2) != 17) return 15;
    int value = 19;
    if (ui_div(-19, 7) != 17 || ui_load(&value) != 17) return 7;
    if (ui_float_conversion(7.5, 3.0) != 17 || ui_fp_destination(3.25) != 17) return 8;
    if (ui_cleanup(7) != 17 || ui_seen != 18 || ui_cleaned != 7) return 9;
    if (ui_braced(11) != 17 || ui_seen != 29 || ui_zero() != 17) return 10;
    if (ui_vla(3) != 17 || ui_seen != 32 || ui_static() != 17 || ui_used(3) != 17) return 11;
    if (ui_escape(3) != 17 || ui_seen != 78 || ui_noopt(3) != 17) return 12;
    for (int i = 0; i < 4; ++i)
        if (ui_inline_a(i) != i + 3 || ui_inline_b(i) != i * 2 + 7) return 13;
#if defined(__x86_64__) || defined(__aarch64__)
    if (ui_fixed(3) != 17) return 14;
#endif
    return 0;
}
