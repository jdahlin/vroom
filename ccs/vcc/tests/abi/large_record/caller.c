#include "record.h"
static struct large_record x, y;
static struct large_record (*volatile indirect)(long, struct large_record, double, struct large_record, long) = transform;
__attribute__((noinline)) static int check(struct large_record r, int offset)
{
    for (unsigned i = 0; i < sizeof r.bytes; i++)
        if (r.bytes[i] != (unsigned char)((unsigned char)(i * 7 + 3) + (unsigned char)(i * 13 + 9) + offset)) return 1;
    return 0;
}
/* Exercise the REP save/restore path with RCX, RSI and RDI live under the
   x86 allocator. Noncommutative arithmetic detects swapped scalar restores;
   the record copy also needs its saved address intact. Check behavior only,
   so this remains valid if allocation or instruction printing changes. */
__attribute__((noinline)) static long keep_live(long a, long b, long c, long d, long e, long f, struct large_record r)
{
    return a - (b - (c - (d - (e - (f + check(r, 51))))));
}
int main(void)
{
    for (unsigned i = 0; i < sizeof x.bytes; i++) {
        x.bytes[i] = (unsigned char)(i * 7 + 3);
        y.bytes[i] = (unsigned char)(i * 13 + 9);
    }
    struct large_record r = transform(11, x, 17.0, y, 23);
    if (check(r, 51)) return 1;
    if (keep_live(1, 2, 4, 8, 16, 32, r) != -21) return 4;
    r = indirect(5, y, 7.0, x, 13);
    if (check(r, 25)) return 2;
    if (x.bytes[0] != 3 || y.bytes[sizeof y.bytes - 1] != (unsigned char)((sizeof y.bytes - 1) * 13 + 9)) return 3;
    return 0;
}
