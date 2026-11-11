/* Bit-field promotion survives assignment, comma and prefix expressions,
   but not an explicit cast or postfix increment. Those expressions remain
   valid sizeof/typeof operands even when their value promotes as a bit-field. */
struct Bits { unsigned small:3, medium:25, full:32; signed negative:3; };
static struct Bits bits;
static int calls;
static struct Bits *next(void) { ++calls; return &bits; }
#define TYPE(e) _Generic(+(e), int:1, unsigned int:2, default:3)
_Static_assert(TYPE(bits.small = 1) == 1, "assignment promotion");
_Static_assert(TYPE(bits.small ^= 7) == 1, "compound promotion");
_Static_assert(TYPE((0, bits.medium)) == 1, "comma promotion");
_Static_assert(TYPE(++bits.small) == 1, "prefix promotion");
_Static_assert(TYPE(bits.small++) == 2, "postfix declared type");
_Static_assert(TYPE((unsigned)bits.small) == 2, "explicit cast");
_Static_assert(TYPE(bits.full = 1) == 2, "full-width field");
_Static_assert(sizeof(bits.small = 1) == sizeof(unsigned), "assignment size");
_Static_assert(sizeof((0, bits.small)) == sizeof(unsigned), "comma size");
_Static_assert(__builtin_types_compatible_p(__typeof__(bits.small = 1), unsigned), "typeof is not promotion");
int main(void)
{
    bits.small = 0;
    if (!((bits.small ^= 255) >= -90364186) || bits.small != 7) return 1;
    bits.medium = 2000;
    if (!((0, bits.medium) > -1)) return 2;
    bits.small = 0; calls = 0;
    if (!((next()->small = 6) > -1) || calls != 1 || bits.small != 6) return 3;
    if (!((++next()->small) > -1) || calls != 2 || bits.small != 7) return 4;
    if (!((calls++, (next()->small = 3)) > -1) || calls != 4) return 5;
    bits.negative = -1;
    if ((bits.negative = -2) >= 0 || (0, bits.negative) >= 0) return 6;
    return 0;
}
