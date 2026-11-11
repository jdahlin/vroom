/* _ExtInt is a deprecated spelling of the same bit-precise integer type. */
#define CHECK(N) \
    _Static_assert(__builtin_types_compatible_p(_ExtInt(N), _BitInt(N)), "signed identity"); \
    _Static_assert(__builtin_types_compatible_p(unsigned _ExtInt(N), unsigned _BitInt(N)), "unsigned identity"); \
    _Static_assert(sizeof(_ExtInt(N)) == sizeof(_BitInt(N)), "layout identity")
CHECK(6); CHECK(14); CHECK(29); CHECK(58); CHECK(120);
static _ExtInt(29) increment(_BitInt(29) x) { return x + 1; }
static _BitInt(29) increment(_ExtInt(29) x);
int main(void)
{
    _ExtInt(120) x = (_ExtInt(120))1 << 100;
    unsigned _ExtInt(6) y = 63;
    y++;
    if (y || x != ((_BitInt(120))1 << 100)) return 1;
    if (increment(-11) != -10) return 2;
    return 0;
}
