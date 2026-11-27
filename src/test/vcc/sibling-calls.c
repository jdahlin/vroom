/* Calls in tail position are jumps at -O1 and up (-foptimize-sibling-calls), unless something in
   the function lets the address of its frame out.  The deep chains overflow the stack if a link is
   a call; the rest read locals through pointers after the callee has used the stack, which a wrong
   jump would have overwritten. */
extern int _setjmp(long *) __attribute__((returns_twice));
extern __attribute__((noreturn)) void longjmp(long *, int);
#include <stdio.h>

#ifdef __OPTIMIZE__
#define DEPTH 3000000L
#else
#define DEPTH 3000L
#endif
#define NOINLINE __attribute__((noinline))

typedef float F __attribute__((vector_size(16)));
typedef struct op op;
typedef void (*step_fn)(const op *program, long n, F a, F b, double x);
struct op {
    step_fn fn;
};

static F vector_result;
static double double_result;

/* a void call at the end of a void function, through a pointer, vectors in registers */
static void finish(const op *program, long n, F a, F b, double x)
{
    (void)program;
    vector_result = a + b;
    double_result = x + (double)n;
}

static NOINLINE void step(const op *program, long n, F a, F b, double x)
{
    if (!n) {
        finish(program, n, a, b, x);
        return;
    }
    program->fn(program, n - 1, b, a + 1.0f, x + 1.0);
}

/* conditional and multiple returns, mutual recursion */
static int is_odd(long n);
static NOINLINE int is_even(long n)
{
    if (!n)
        return 1;
    return is_odd(n - 1);
}
static NOINLINE int is_odd(long n)
{
    if (!n)
        return 0;
    return is_even(n - 1);
}

static NOINLINE double sum_double(long n, double a, double b)
{
    if (n & 1)
        return sum_double(n - 1, b, a + 1);
    if (n)
        return sum_double(n - 1, a + 2, b);
    return a + b;
}

static NOINLINE const char *walk_pointer(const char *p, long n)
{
    switch (n & 3) {
    case 0:
        if (!n)
            return p;
        return walk_pointer(p, n - 1);
    default:
        return walk_pointer(p, n - 1);
    }
}

/* a record result in two registers passed on, and one that must not be: the members swapped */
struct two {
    long a, b;
};
static NOINLINE struct two pair_chain(long n, long a, long b)
{
    if (!n)
        return (struct two){a, b};
    return pair_chain(n - 1, a + 1, b + 2);
}
static NOINLINE struct two pair_swapped(long n)
{
    struct two t = pair_chain(n, 0, 0);
    return (struct two){t.b, t.a};
}

/* negative cases: each callee reads the caller's frame after using the stack itself */
static int *saved;

static NOINLINE int churn(int k)
{
    volatile int pad[64];
    for (int i = 0; i < 64; i++)
        pad[i] = -1 - k;
    return pad[k & 63] + k + 1;
}

static NOINLINE int read_pointer(const int *p)
{
    int a = churn(1), b = churn(2);
    return *p + a + b;
}

static NOINLINE int read_saved(int k)
{
    int a = churn(k), b = churn(k);
    return *saved + a + b;
}

struct big {
    long v[6];
};
static NOINLINE int read_struct(struct big b)
{
    int a = churn(1);
    return (int)b.v[5] + a;
}

struct pair {
    int *p;
    long n;
};
static NOINLINE int read_pair(struct pair q)
{
    int a = churn(1), b = churn(2);
    return *q.p + (int)q.n + a + b;
}

static NOINLINE int local_argument(int v)
{
    int x = v;
    return read_pointer(&x);
}
static NOINLINE int local_array(int v)
{
    int x[4] = {v, v + 1, v + 2, v + 3};
    return read_pointer(x + (v & 3));
}
static NOINLINE int local_stored(int v)
{
    int x = v;
    saved = &x;
    return read_saved(1);
}
static NOINLINE int parameter_address(int v)
{
    saved = &v;
    return read_saved(2);
}
static NOINLINE int local_member(int v)
{
    struct big b = {{v, v, v, v, v, v}};
    int m = (int)b.v[v & 1];
    saved = &m;
    return read_saved(3);
}
static NOINLINE int local_in_register_struct(int v)
{
    int x = v;
    struct pair q = {&x, v};
    return read_pair(q);
}
static NOINLINE int local_struct_by_value(int v)
{
    struct big b = {{v, v, v, v, v, v}};
    return read_struct(b);
}
static NOINLINE int local_alloca(int v)
{
    int *x = (int *)__builtin_alloca((size_t)(v & 7) * sizeof *x + sizeof *x);
    *x = v;
    return read_pointer(x);
}
static NOINLINE int local_vla(int v)
{
    int x[(v & 7) + 1];
    x[0] = v;
    return read_pointer(x);
}
static NOINLINE int local_compound(int v)
{
    return read_pointer((int[]){v, v + 1});
}
static NOINLINE int local_in_loop(int v)
{
    int x = v, y = 0;

    for (int i = 0; i < 3; i++) {
        if (i == 2)
            return read_saved(4);
        y += i;
        saved = &x;
    }
    return y;
}

static long jump[64];
static NOINLINE int jump_back(int v)
{
    if (!churn(v))
        longjmp(jump, 1);
    return 0;
}
static NOINLINE int calls_setjmp(int v)
{
    volatile int x = v;
    if (_setjmp(jump))
        return x + 1;
    return jump_back(v);
}

int main(void)
{
    static const op program = {step};
    F a = {1, 2, 3, 4}, b = {5, 6, 7, 8};

    step(&program, DEPTH, a, b, 0.5);
    /* (the output must not depend on DEPTH: clang's reference is built at -O2) */
    printf("%g %g %g\n", (double)(vector_result[0] - (float)DEPTH),
           (double)(vector_result[3] - (float)DEPTH),
           double_result - (double)DEPTH);
    printf("%d %d\n", is_even(DEPTH), is_odd(DEPTH + 1));
    printf("%g\n", sum_double(DEPTH, 0, 0) - 1.5 * (double)DEPTH);
    printf("%s\n", walk_pointer("pointer", DEPTH));
    struct two p = pair_chain(DEPTH, 1, 2), q = pair_swapped(10);
    printf("%ld %ld %ld %ld\n", p.a - DEPTH, p.b - 2 * DEPTH, q.a, q.b);
    printf("%d %d %d %d %d\n", local_argument(10), local_array(20),
           local_stored(30), parameter_address(40), local_member(50));
    printf("%d %d %d %d %d %d\n", local_in_register_struct(60),
           local_struct_by_value(70), local_alloca(80), local_vla(90),
           local_compound(100), local_in_loop(110));
    printf("%d\n", calls_setjmp(5));
    return 0;
}
