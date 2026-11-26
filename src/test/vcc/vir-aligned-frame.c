/* Frame objects aligned past 16 (VIR x86-64 realigns the frame): their
   addresses and contents. */
#include <stdio.h>
#include <stdint.h>
typedef int v8 __attribute__((vector_size(32)));
__attribute__((noinline)) static int check(int *p) { return (int)((uintptr_t)p & 31); }
__attribute__((noinline)) static int f(int a, int b, int c, int d, int e, int g, int h, int i)
{
    _Alignas(32) int local[16];
    _Alignas(64) char other[40];
    int s = 0;
    for (int k = 0; k < 16; k++) local[k] = a * k + b + c + d + e + g + h + i;
    other[0] = 1;
    s = check(local) * 1000 + ((int)((uintptr_t)other & 63)) * 100;
    for (int k = 0; k < 16; k++) s += local[k];
    return s + other[0];
}
int main(void) { printf("%d\n", f(1, 2, 3, 4, 5, 6, 7, 8)); return 0; }
