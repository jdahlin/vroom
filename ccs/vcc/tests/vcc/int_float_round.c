/* Integer constants must round directly to float, without a double midpoint. */
typedef unsigned long long U64;
typedef __int128 I128;
typedef unsigned __int128 U128;
static volatile float out;
static volatile long double ldout;
int main(void)
{
    out = ((1LL << 62) + (1LL << 38) + 1);
    if (out != 0x1.000002p62f) return 1;
    out = -((1LL << 62) + (1LL << 38) + 1);
    if (out != -0x1.000002p62f) return 2;
    out = ((U64)1 << 63) + ((U64)1 << 39) + 1;
    if (out != 0x1.000002p63f) return 3;
    out = ((I128)1 << 126) + ((I128)1 << 102) + 1;
    if (out != 0x1.000002p126f) return 4;
    out = -(((I128)1 << 126) + ((I128)1 << 102) + 1);
    if (out != -0x1.000002p126f) return 5;
    out = ((U128)1 << 127) + ((U128)1 << 103) + 1;
    if (out != 0x1.000002p127f) return 6;
    ldout = (I128)(((U128)1 << 127) - 1);
    if (ldout != 0x1p127L) return 7;
    ldout = ~(U128)0;
    if (ldout != 0x1p128L) return 8;
    return 0;
}
