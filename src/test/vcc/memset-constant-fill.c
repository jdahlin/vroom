/* memset of a constant byte and a small constant size is stored in place, any
 * byte value, as clang: every length up to 64, the destination returned,
 * nothing written outside it. */
void *memset(void *, int, __SIZE_TYPE__);
#define ONE(n, byte)                                                          \
    do {                                                                      \
        unsigned char b[80];                                                  \
        for (int i = 0; i < 80; i++)                                          \
            ((volatile unsigned char *)b)[i] = 0x5a;                          \
        if (memset(b + 3, byte, n) != b + 3)                                  \
            return 1;                                                         \
        for (int i = 0; i < 80; i++)                                          \
            if (((volatile unsigned char *)b)[i] !=                           \
                (i >= 3 && i < 3 + (n) ? (unsigned char)(byte) : 0x5a))       \
                return 2;                                                     \
    } while (0)
#define ALL(n)                                                                \
    ONE(n, 0);                                                                \
    ONE(n, 1);                                                                \
    ONE(n, -1);                                                               \
    ONE(n, 0x1a5)
int main(void)
{
    ALL(1);
    ALL(2);
    ALL(3);
    ALL(4);
    ALL(5);
    ALL(7);
    ALL(8);
    ALL(9);
    ALL(15);
    ALL(16);
    ALL(31);
    ALL(33);
    ALL(64);
    return 0;
}
