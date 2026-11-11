#include "record.h"
struct large_record transform(long a, struct large_record x, double b, struct large_record y, long c)
{
    struct large_record result;
    result = x;
    for (unsigned i = 0; i < sizeof result.bytes; i++)
        result.bytes[i] = (unsigned char)(x.bytes[i] + y.bytes[i] + a + (long)b + c);
    /* Both incoming values are private copies. */
    x.bytes[0] = 0;
    y.bytes[sizeof y.bytes - 1] = 0;
    return result;
}
