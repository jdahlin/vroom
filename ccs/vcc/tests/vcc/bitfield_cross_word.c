/* Packed fields crossing a machine word: loads, stores, signs and neighbours. */
struct __attribute__((packed)) Packed {
    unsigned char prefix : 4;
    unsigned long long wide : 64;
    signed long long signed_wide : 60;
    unsigned tail : 7;
};
struct __attribute__((packed)) Offset {
    unsigned long long pad : 61;
    signed int field : 15;
    unsigned suffix : 3;
};
static struct Packed global;
static int check(struct Packed *p)
{
    p->prefix = 11;
    p->tail = 93;
    p->signed_wide = -37;
    p->wide = 0xfedcba9876543210ULL;
    if (p->prefix != 11 || p->tail != 93 || p->signed_wide != -37 ||
        p->wide != 0xfedcba9876543210ULL) return 1;
    p->wide += 7;
    p->signed_wide -= 9;
    if (p->wide != 0xfedcba9876543217ULL || p->signed_wide != -46 ||
        p->prefix != 11 || p->tail != 93) return 2;
    p->wide = 0;
    if (p->wide || p->signed_wide != -46 || p->prefix != 11 || p->tail != 93) return 3;
    return 0;
}
int main(void)
{
    struct Packed local = { 0 };
    struct Offset o = { 0x123456789abcdefULL, -1234, 5 };
    if (check(&local) || check(&global)) return 1;
    if (o.field != -1234 || o.suffix != 5 || o.pad != 0x123456789abcdefULL) return 2;
    o.field++;
    if (o.field != -1233 || o.suffix != 5 || o.pad != 0x123456789abcdefULL) return 3;
    return 0;
}
