// operator<=> and the relational operators rewritten from it:
// [over.match.oper]/3.4, /8
#include <stdio.h>

struct Version {
    int major, minor;
    int operator<=>(const Version &o) const
    {
        if (major != o.major)
            return major - o.major;
        return minor - o.minor;
    }
};

struct Meters {
    int v;
};
// a non-member: also found reversed, for int <=> Meters
int operator<=>(Meters a, int b) { return a.v - b; }

struct Both {
    int v;
    int operator<=>(Both o) const { return v - o.v; }
    bool operator<(Both o) const { return v * 2 < o.v; } // preferred to <=>
};

int main()
{
    Version a{1, 2}, b{1, 5}, c{2, 0};
    bool lt = a < b, lt2 = b < c, gt = c > a, le = a <= a, ge = b >= c;
    int total = lt + lt2 * 2 + gt * 4 + le * 8 + ge * 16; // 1 + 2 + 4 + 8
    total += (a <=> b) * -1;                             // 3
    Meters m{7};
    bool m1 = m < 10, m2 = 10 < m, m3 = 3 < m;
    total += m1 * 32 + m2 * 64 + m3 * 128;               // 32 + 128
    Both x{3}, y{5};
    bool x1 = x < y, x2 = x > y, x3 = x <= y;  // operator<; then by <=>
    total += x1 * 1000 + x2 * 2000 + x3 * 10000;         // 10000
    printf("%d\n", total);
    return total & 255;
}
