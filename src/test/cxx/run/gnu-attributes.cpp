// [ext:gnu_attributes] GCC's attributes before a declaration, among its
// decl-specifiers, after its declarator and after a function's parameters;
// packed members; unsigned 64-bit constants widened to unsigned __int128
typedef unsigned __int128 u128;

__attribute__((noinline)) int before(int a) { return a + 1; }
int __attribute__((noinline, noclone)) among(int a) { return a + 2; }
static int after(int) __attribute__((always_inline, unused));
static int after(int a) { return a * 3; }
__attribute__((weak)) int weak_one(void) { return 4; }
__attribute__((noreturn)) void stop(void);

char aligned_default[3] __attribute__((aligned));
long aligned_eight __attribute__((__aligned__(8))) = 5;
int in_section __attribute__((section(".data.vcc_test"))) = 6;
__attribute__((used)) static int kept = 7;

struct Packed {
    char c;
    int i __attribute__((packed));
    short s __attribute__((packed, aligned(2)));
};

struct Plain {
    char c;
    int i;
};

u128 widen(void) { return 0xfedcba9876543210ULL; }

int main() {
    int local __attribute__((aligned(32))) = 8;
    if (((unsigned long)&local & 31) != 0)
        return 1;
    if (((unsigned long)aligned_default & 15) != 0)
        return 2;
    if (sizeof(Packed) != 8 || sizeof(Plain) != 8 || alignof(Packed) != 2)
        return 3;
    Packed p;
    p.c = 1;
    p.i = 0x12345678;
    p.s = 9;
    if (p.i != 0x12345678 || p.s != 9)
        return 4;
    u128 w = widen();
    if ((unsigned long long)(w >> 64) != 0 ||
        (unsigned long long)w != 0xfedcba9876543210ULL)
        return 5;
    return before(1) + among(1) + after(1) + weak_one() + aligned_eight +
           in_section + kept + local - 38;
}
