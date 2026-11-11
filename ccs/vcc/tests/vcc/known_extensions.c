/* Canonical unsigned results may omit a second zero extension. */
#define NI __attribute__((noinline))
typedef unsigned long long U;
NI U ke_load(const unsigned *p) { return *p; }
NI U ke_add(unsigned x, unsigned y) { return x + y; }
NI U ke_index(const unsigned *p, unsigned i) { return p[i]; }
NI U ke_wide(U x) { return (unsigned)x; }
NI U ke_negative(const int *p) { return (unsigned)*p; }
NI long long ke_signed(int x, int y) { return x + y; }
NI U ke_shift(U x, unsigned n) { return (unsigned)x << n; }
NI U ke_byte(const unsigned char *p) { return (unsigned)*p; }
NI U ke_volatile(const volatile unsigned *p) { return *p; }
int main(void)
{
    unsigned a[] = {0x80000000u, 0xffffffffu};
    int negative = -1;
    unsigned char byte = 255;
    volatile unsigned v = 0xffffffffu;
    if (ke_load(a) != 0x80000000ULL || ke_load(a + 1) != 0xffffffffULL) return 1;
    if (ke_add(0xffffffffu, 2) != 1 || ke_add(0x7fffffffu, 1) != 0x80000000ULL) return 2;
    if (ke_index(a, 1) != 0xffffffffULL) return 3;
    if (ke_wide(0xaabbccdd80000000ULL) != 0x80000000ULL) return 4;
    if (ke_wide(0xffffffffffffffffULL) != 0xffffffffULL) return 5;
    if (ke_negative(&negative) != 0xffffffffULL || ke_signed(-1, -2) != -3) return 6;
    if (ke_shift(0xaabbccdd80000000ULL, 0) != 0x80000000ULL) return 7;
    if (ke_shift(0xffff000000000001ULL, 31) != 0x80000000ULL) return 8;
    if (ke_byte(&byte) != 255 || ke_volatile(&v) != 0xffffffffULL) return 9;
    return 0;
}
