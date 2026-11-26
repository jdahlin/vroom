/* Static functions taking and returning 32-byte vectors (VIR on x86-64 passes
   none: they are inlined). */
#include <stdio.h>

typedef int v8 __attribute__((vector_size(32)));

static v8 mix(v8 a, v8 b, int n)
{
    v8 r = a;
    for (int i = 0; i < n; i++) {
        r = r + b;
        r = r ^ a;
        if (i & 1)
            r = r - 3;
    }
    return r;
}

static void add_into(v8 *sum, v8 a, v8 b)
{
    v8 first = a * b;
    v8 second = first + a;
    v8 third = second ^ b;
    *sum = *sum + third;
}

__attribute__((noinline)) static void run(v8 *out, const v8 *in, int n)
{
    v8 sum = {0};
    for (int i = 0; i < n; i++) {
        add_into(&sum, in[i % 4], in[(i + 1) % 4]);
        sum = mix(sum, in[i % 4], i % 5);
    }
    *out = sum;
}

int main(void)
{
    v8 in[4], out;
    for (int k = 0; k < 4; k++)
        for (int i = 0; i < 8; i++)
            in[k][i] = k * 100 + i * 7 - 50;
    run(&out, in, 23);
    for (int i = 0; i < 8; i++)
        printf("%d ", out[i]);
    printf("\n");
    return 0;
}
