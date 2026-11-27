// calls in tail position are jumps at -O1 and up (-O0 runs shallow chains);
// a local whose address an inlined or called function got keeps its frame
extern "C" int printf(const char *, ...);

#ifdef __OPTIMIZE__
constexpr long depth = 3000000;
#else
constexpr long depth = 3000;
#endif

typedef float F __attribute__((vector_size(16)));

struct stage {
    void (*fn)(const stage *program, long n, F r, F g);
};

static float out;

__attribute__((noinline)) static void finish(F r, F g) { out = r[0] + g[0]; }

__attribute__((noinline)) static void run(const stage *program, long n, F r,
                                          F g)
{
    if (!n) {
        finish(r, g);
        return;
    }
    auto next = program->fn;
    next(program, n - 1, g, r + 1.0f);
}

static const int *saved;

__attribute__((noinline)) static int churn(int k)
{
    volatile int pad[64];
    for (int i = 0; i < 64; i++)
        pad[i] = -1 - k;
    return pad[k & 63] + k + 1;
}

__attribute__((noinline)) static int read_reference(const int &x)
{
    int a = churn(1), b = churn(2);
    return x + a + b;
}

__attribute__((noinline)) static int read_saved(int k)
{
    int a = churn(k), b = churn(k);
    return *saved + a + b;
}

struct holder {
    int v;
    void keep() const { saved = &v; }
};

__attribute__((noinline)) static int temporary(int v)
{
    return read_reference(v + 1);
}
__attribute__((noinline)) static int member(int v)
{
    holder h{v};
    h.keep();
    return read_saved(1);
}

int main()
{
    static const stage program = {run};
    F r = {1, 1, 1, 1}, g = {2, 2, 2, 2};

    run(&program, depth, r, g);
    printf("%d\n", out > 0.0f);
    printf("%d %d\n", temporary(10), member(20));
}
