// the address of an overload set: [over.over]
#include <stdio.h>

int f(int x) { return x + 1; }
int f(double x) { return int(x * 2); }
int f(int x, int y) { return x * y; }

typedef int (*unary_int)(int);
typedef int (*unary_double)(double);

static int apply(unary_int g, int v) { return g(v); }
static int apply2(int (*g)(int, int)) { return g(6, 7); }

static unary_double pick() { return &f; } // the return value

int main()
{
    unary_int a = f;                 // an object initialized
    unary_double b = &f;             // ... by &f
    int (&r)(int, int) = f;          // a reference initialized
    int (*c)(int) = (f);             // parentheses ignored
    unary_int d;
    d = &f;                          // the left side of an assignment
    int total = a(1) + b(2.5) + r(3, 4) + c(10) + d(20);  // 2 + 5 + 12 + 11 + 21
    total += apply(f, 100) + apply2(&f);                  // 101 + 42
    total += pick()(4.0);                                 // 8
    total += ((unary_double)f)(1.5) + ((unary_int)&f)(0); // 3 + 1
    printf("%d\n", total);
    return total & 255;
}
