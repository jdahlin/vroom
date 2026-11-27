/* __attribute__((musttail)): every call here is a jump.  Each chain is deep enough to overflow the
   stack many times over if any link were an ordinary call. */
#include <stdio.h>

#define TAIL __attribute__((musttail))
#define DEPTH 3000000L

typedef float F __attribute__((vector_size(16)));
typedef struct op op;
typedef long (*step_fn)(const op *program, long n, long a, long b, double x);
struct op {
    step_fn fn;
};

/* direct, mutual recursion */
static int is_odd(long n);
static __attribute__((noinline)) int is_even(long n)
{
    if (!n)
        return 1;
    TAIL return is_odd(n - 1);
}
static __attribute__((noinline)) int is_odd(long n)
{
    if (!n)
        return 0;
    TAIL return is_even(n - 1);
}

/* indirect, arguments permuted, locals in callee-saved registers and a frame */
static long finish(const op *program, long n, long a, long b, double x)
{
    (void)program;
    return n + a * 3 + b * 5 + (long)x;
}

static long swap(const op *program, long n, long a, long b, double x)
{
    volatile char pad[200];

    pad[n & 127] = 1;
    if (!n)
        TAIL return program[1].fn(program, n, a, b, x);
    TAIL return program->fn(program, n - pad[n & 127], b, a, x + 1.0);
}

static long busy(long n, long a, long b, long c, long d, long e)
{
    long p = a * b + c, q = b * c + d, r = c * d + e, s = d * e + a,
         t = e * a + b;

    if (!n)
        return p + q + r + s + t;
    TAIL return busy(n - 1, t % 1000, s % 1000, r % 1000, q % 1000,
                     p % 1000);
}

/* void result, vector arguments */
typedef void (*stage)(void **program, long n, F a, F b, F c, F d);
static float out[4];

static void done(void **program, long n, F a, F b, F c, F d)
{
    F s = a + b + c + d;

    (void)program, (void)n;
    for (int i = 0; i < 4; i++)
        out[i] = s[i];
}

static void add(void **program, long n, F a, F b, F c, F d)
{
    if (!n)
        TAIL return ((stage)program[1])(program, n, a, b, c, d);
    a += 1.0f;
    TAIL return ((stage)program[0])(program, n - 1, a, b, c, d);
}

/* a record in registers, a narrow result, a floating result, a variable-length array */
struct pair {
    long a, b;
};

static struct pair count(long n, struct pair p)
{
    if (!n)
        return p;
    p.a++;
    p.b += 2;
    TAIL return count(n - 1, p);
}

static signed char narrow(long n, signed char x)
{
    if (!n)
        return x;
    TAIL return narrow(n - 1, (signed char)(x + 1));
}

static double sum(long n, double x, int k)
{
    int local[8];

    local[n & 7] = k;
    if (!n)
        return x + local[0] * 0;
    __attribute__((musttail)) return sum(n - 1, x + 1, local[n & 7]);
}

static long vla(long n)
{
    int a[(n & 15) + 1];

    a[0] = 1;
    if (!n)
        return a[0];
    TAIL return vla(n - a[0]);
}

/* a callee declared elsewhere in the library, kept a call */
static int say(const char *s)
{
    TAIL return puts(s);
}

int main(void)
{
    op program[2] = {{swap}, {finish}};
    void *stages[2] = {(void *)add, (void *)done};
    F z = {0, 1, 2, 3};
    struct pair q = count(DEPTH, (struct pair){0, 0});

    add(stages, DEPTH, z, z, z, z);
    printf("%d %d\n", is_even(DEPTH), is_odd(DEPTH + 1));
    printf("%ld %ld\n", swap(program, DEPTH + 1, 7, 11, 0.5),
           busy(DEPTH, 1, 2, 3, 4, 5));
    printf("%g %g %g %g\n", out[0], out[1], out[2], out[3]);
    printf("%ld %ld %d %g %ld\n", q.a, q.b, narrow(DEPTH, 0),
           sum(DEPTH, 0, 1), vla(DEPTH));
    say("done");
    return 0;
}
