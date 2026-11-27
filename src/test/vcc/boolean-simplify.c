/* Choices of 1 and 0 become comparisons, and values that are already 0 or 1
 * are not compared with zero again: the results over every kind of input. */
#include <stdbool.h>
int printf(const char *, ...);
struct s {
    bool f, g;
    int v;
};
__attribute__((noinline)) static int negation(int x) { bool n = !x; return n; }
__attribute__((noinline)) static int both(int x, int y) { return x && y; }
__attribute__((noinline)) static int either(bool p, int x)
{
    bool q = p || x > 3;
    bool r = !q;
    return q * 10 + r;
}
__attribute__((noinline)) static int odd_choices(int x, int y)
{
    int a = x ? 1 : 0, b = x ? 0 : 1, c = x ? 2 : 0, d = x ? -1 : 0;
    int e = (x > y) ? 1 : 0, f = (x > y) ? 0 : 1, g = (x ? y : 1) != 0;
    return a + b * 10 + (c != 0) * 100 + (d != 0) * 1000 + e * 10000 +
           f * 100000 + g * 1000000;
}
__attribute__((noinline)) static int carried(const struct s *p, int n)
{
    bool seen = false;
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (p[i].v > 2)
            seen = true;
        count += seen != false;
        seen = !seen;
    }
    return count * 2 + (seen != 0);
}
__attribute__((noinline)) static int floats(double x)
{
    int a = x == x ? 1 : 0, b = x < 1.0 ? 0 : 1;
    return a * 2 + (b != 0);
}
int main(void)
{
    int in[] = {0, 1, -1, 2, 5, 100, -2147483647 - 1, 256};
    struct s v[5] = {{0, 0, 1}, {0, 0, 5}, {0, 0, 0}, {0, 0, 9}, {0, 0, 3}};
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            printf("%d %d %d %d\n", negation(in[i]), both(in[i], in[j]),
                   either(i & 1, in[j]), odd_choices(in[i], in[j]));
    for (int n = 0; n <= 5; n++)
        printf("%d\n", carried(v, n));
    printf("%d %d %d\n", floats(0.5), floats(2.0), floats(__builtin_nan("")));
    return 0;
}
