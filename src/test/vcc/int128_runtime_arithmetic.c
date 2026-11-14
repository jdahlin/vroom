typedef __int128 S;
typedef unsigned __int128 U;
#define NI __attribute__((noinline))

static NI S sdiv(S a, S b) { return a / b; }
static NI S srem(S a, S b) { return a % b; }
static NI U udiv(U a, U b) { return a / b; }
static NI U urem(U a, U b) { return a % b; }
static NI float sfloat(S a) { return (float)a; }
static NI double sdouble(S a) { return (double)a; }
static NI float ufloat(U a) { return (float)a; }
static NI double udouble(U a) { return (double)a; }
static NI S floats(float a) { return (S)a; }
static NI S doubles(double a) { return (S)a; }
static NI U floatu(float a) { return (U)a; }
static NI U doubleu(double a) { return (U)a; }

int main(void)
{
	U b = ((U)1 << 70) + 3, q = (U)1 << 55, a = b * q + 11;
	if (udiv(a, b) != q || urem(a, b) != 11) return 1;
	if (sdiv(-(S)a, (S)b) != -(S)q || srem(-(S)a, (S)b) != -11) return 2;
	S negative = -((S)1 << 98);
	U high = (U)1 << 127;
	if (sfloat(negative) != -0x1p98f || sdouble(negative) != -0x1p98) return 3;
	if (ufloat(high) != 0x1p127f || udouble(high) != 0x1p127) return 4;
	if (floats(-0x1p98f) != negative || doubles(-0x1p98) != negative) return 5;
	if (floatu(0x1p127f) != high || doubleu(0x1p127) != high) return 6;
	/* Halfway cases must round once to the destination precision. */
	if (ufloat(((U)1 << 24) + 1) != 0x1p24f ||
	    ufloat(((U)1 << 24) + 3) != 0x1.000004p24f) return 7;
	if (udouble(((U)1 << 53) + 1) != 0x1p53 ||
	    udouble(((U)1 << 53) + 3) != 0x1.0000000000002p53) return 8;
	/* Low limbs can break a tie even when the high limb is an exact tie. */
	if (ufloat(((U)1 << 100) + ((U)1 << 76) + 1) != 0x1.000002p100f) return 9;
	if (sfloat((S)((U)1 << 127)) != -0x1p127f) return 10;
	return 0;
}
