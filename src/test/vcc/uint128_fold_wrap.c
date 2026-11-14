/* Unsigned target arithmetic must not overflow the compiler's signed i128. */
typedef unsigned __int128 U;
#define HIGH ((U)1 << 127)
#define LOW (~HIGH)
_Static_assert(HIGH - 1 == LOW, "subtract across signed minimum");
_Static_assert(LOW + 1 == HIGH, "add across signed maximum");
_Static_assert(-HIGH == HIGH, "negate unsigned high bit");
static U initial[] = { HIGH - 1, LOW + 1, -HIGH };
static volatile U out;
int main(void)
{
    if (initial[0] != LOW || initial[1] != HIGH || initial[2] != HIGH) return 1;
    out = HIGH - 1;
    if (out != LOW) return 2;
    out = LOW + 1;
    if (out != HIGH) return 3;
    out = -HIGH;
    if (out != HIGH) return 4;
    return 0;
}
