// default arguments of member functions, a complete-class context:
// [dcl.fct.default], [class.mem]/6
#include <stdio.h>

struct Scale {
    int factor;
    int apply(int x, int by = later + 1) const { return x * by + factor; }
    static int twice(int x = seed * 2) { return x * 2; }
    int operator()(int a, int b = 10) const { return a + b; }
    int sum(int a, int b = 1, int c = 2) const;
    enum { later = 4, seed = 3 }; // declared after their uses above
};

int Scale::sum(int a, int b, int c) const { return a * 100 + b * 10 + c; }

int main()
{
    Scale s{7};
    int total = s.apply(2);     // 2*5 + 7 = 17
    total += s.apply(2, 3);     // 13
    total += Scale::twice();    // 12
    total += Scale::twice(1);   // 2
    total += s(5) + s(5, 1);    // 15 + 6
    total += s.sum(1) + s.sum(1, 5) + s.sum(1, 5, 9); // 112 + 152 + 159
    printf("%d\n", total);
    return total & 255;
}
