/* x86 uses x87 XF helpers; ARM uses binary128 TF helpers. */
typedef __int128 I;
typedef unsigned __int128 U;
static volatile I signed_input;
static volatile U unsigned_input;
static volatile long double float_input;
static int calls;
static I si(void) { calls++; return signed_input; }
static U ui(void) { calls++; return unsigned_input; }
static long double fp(void) { calls++; return float_input; }
static volatile long double left = 3.0L;
static volatile U addend = 9;
int main(void)
{
    I values[] = {0, 1, -1, (I)1 << 64, -((I)1 << 64),
                  (I)1 << 100, -((I)1 << 100), (I)((U)1 << 127)};
    for (int i = 0; i < 8; i++) {
        signed_input = values[i];
        calls = 0;
        long double x = (long double)si();
        if (calls != 1) return 1;
        float_input = x;
        if ((I)fp() != values[i] || calls != 2) return 2;
    }
    unsigned_input = (U)1 << 127;
    calls = 0;
    long double x = left + (long double)ui();
    if (calls != 1 || x != 0x1p127L) return 3;
    float_input = 0x1p127L;
    if (addend + (U)fp() != ((U)1 << 127) + 9 || calls != 2) return 4;
    signed_input = (I)(((U)1 << 127) - 1);
    unsigned_input = ~(U)0;
    if ((long double)si() != 0x1p127L || (long double)ui() != 0x1p128L) return 5;
    float_input = 0x1.fffffffffffffffep126L;
    if ((I)fp() != (I)(((U)1 << 127) - ((U)1 << 63))) return 6;
    float_input = 0x1.fffffffffffffffep127L;
    if ((U)fp() != (U)0 - ((U)1 << 64)) return 7;
    float_input = -0.75L;
    if ((I)fp() != 0 || (U)fp() != 0) return 8;
    return 0;
}
