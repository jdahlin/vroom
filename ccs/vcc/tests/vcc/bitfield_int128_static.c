typedef unsigned __int128 U;
typedef __int128 S;
struct Signed { S value:66; } signed_values[] = {
    { 1 }, { 10000 }, { 0x12345 }, { 0xff000001 },
    { (U)1 << 63 }, { (U)1 << 64 }, { ((U)1 << 64) | 1 },
    { -1 }, { -((S)1 << 64) }
};
struct __attribute__((packed)) Packed {
    unsigned lead:3;
    U value:119;
    unsigned tail:6;
} packed_values[] = {
    { 5, (U)1 << 100, 37 },
    { 3, ((U)1 << 118) | 0x123456789abcdef0ULL, 12 },
    { 7, (~(U)0) >> 9, 63 }
};
struct Full { U value:128; } full_value = { ((U)1 << 127) | 23 };
static __attribute__((noinline)) int check_signed(unsigned i, S expected)
{
    return signed_values[i].value == expected;
}
static __attribute__((noinline)) int check_packed(unsigned i, U expected, unsigned lead, unsigned tail)
{
    return packed_values[i].value == expected && packed_values[i].lead == lead && packed_values[i].tail == tail;
}
int main(void)
{
    S expected[] = { 1, 10000, 0x12345, 0xff000001, (U)1 << 63,
                     (U)1 << 64, ((U)1 << 64) | 1, -1, -((S)1 << 64) };
    for (unsigned i = 0; i < 9; i++)
        if (!check_signed(i, expected[i])) return 1 + i;
    if (!check_packed(0, (U)1 << 100, 5, 37)) return 10;
    if (!check_packed(1, ((U)1 << 118) | 0x123456789abcdef0ULL, 3, 12)) return 11;
    if (!check_packed(2, (~(U)0) >> 9, 7, 63)) return 12;
    if (full_value.value != (((U)1 << 127) | 23)) return 13;
    return 0;
}
