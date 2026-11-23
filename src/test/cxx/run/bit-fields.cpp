// [class.bit]: bit-fields laid out as clang's for aarch64 Linux, unnamed
// and zero-width ones, [conv.prom]/5's promotions, aggregate
// initialization skipping unnamed bit-fields ([class.bit]/2)
struct Flags {
    unsigned a : 3;
    unsigned b : 5;
    int c : 4;
    unsigned : 0;
    unsigned d : 31;
    unsigned long long e : 40;
    char f : 2;
    bool g : 1;
};

struct Holes {
    char x;
    int : 12;
    short y : 7;
    unsigned z : 32;
};

struct Init {
    int one : 4;
    int : 4;
    int two : 8;
};

static Flags global = {5, 17, -3, 100, 0x123456789ULL, 1, true};

int main() {
    if (sizeof(Flags) != 16 || alignof(Flags) != 8)
        return 1;
    if (sizeof(Holes) != 8 || alignof(Holes) != 4)
        return 2;
    Flags f = {};
    f.a = 9; /* wraps to 1 */
    f.c = 7;
    f.c += 2; /* 9 does not fit: -7 */
    f.d = 0x7fffffff;
    if (f.a != 1 || f.c != -7 || f.d != 0x7fffffffu)
        return 3;
    /* [conv.prom]/5: an unsigned bit-field narrower than int promotes to
       int, one of 32 bits to unsigned int */
    if (f.a - 2 >= 0)
        return 4;
    if (f.d - 0x80000000u < 0x80000000u)
        return 5;
    if (global.a != 5 || global.b != 17 || global.c != -3 || global.d != 100 ||
        global.e != 0x123456789ULL || global.f != 1 || !global.g)
        return 6;
    Init i = {3, -100};
    if (i.one != 3 || i.two != -100)
        return 7;
    Holes h;
    h.x = 1;
    h.y = -64;
    h.z = 0xffffffffu;
    Holes copy = h;
    if (copy.y != -64 || copy.z != 0xffffffffu)
        return 8;
    unsigned sum = 0;
    for (f.b = 0; f.b < 20; f.b++)
        sum += f.b;
    return sum == 190 ? 0 : 9;
}
