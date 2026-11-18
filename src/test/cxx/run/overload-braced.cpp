// overload resolution with braced-init-list arguments: [over.ics.list]/10
#include <stdio.h>

static int f(int x) { return x * 10; }
static int f(double x) { return int(x * 100); }
static int f(const char *s) { return s[0]; }

static int g(int m, int k) { return m * 1000 + k; }
static int g(long v, int k) { return int(v) + k; }

struct S {
    int h(char c) const { return c; }
    int h(long v) const { return int(v) * 2; }
};

int main()
{
    int total = f({7});         // f(int): 70
    total += f({2.5});          // f(double): 250
    total += f({"A"});          // f(const char *): 65
    total += f({'a'}) / 97;     // char to int is a promotion: f(int), 10
    total += g({1}, 5);         // 1005
    total += g({3L}, {4});      // 7
    S s;
    total += s.h({'b'}) + s.h({21L}); // 98 + 42
    printf("%d\n", total);
    return total & 255;
}
