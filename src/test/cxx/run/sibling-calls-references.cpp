// sibling calls after a callee was passed the address of a local: a jump when the callee provably
// drops it before returning (the neg_ functions below must keep their call: each hands the address
// on, and reads it after the callee has used the stack, which a jump would have overwritten)
extern "C" int printf(const char *, ...);
extern "C" void *memcpy(void *, const void *, decltype(sizeof 0));

#ifdef __OPTIMIZE__
constexpr long depth = 3000000;
#else
constexpr long depth = 3000;
#endif
#define NOINLINE __attribute__((noinline))
#define SI __attribute__((always_inline)) static inline

typedef float F __attribute__((vector_size(16)));

// ---- positive: a chain of stages as Skia's raster pipeline, which overflows the stack if a link
// is a call

struct stage;
typedef void (*stage_fn)(const stage *, long, F, F, F, F);
struct stage {
    stage_fn fn;
};
static float out;

template <class D, class S> SI D load(const S *p)
{
    D d;
    memcpy(&d, p, sizeof d);
    return d;
}
SI const float &smaller(const float &a, const float &b) { return b < a ? b : a; }
static inline void set_two(float *x, float *y) { *x += 1.0f, *y += 2.0f; }
template <class T> static inline T &&as_moved(T &t) { return static_cast<T &&>(t); }

SI void stage_k(F &r, F &g, F &b, F &a)
{
    F x = r, &alias = r;
    float lane0 = load<float>(&x), one = 1.0f;
    F *table[] = {&g, &b};
    int swap = lane0 > 0.0f;
    float low = smaller(lane0, one), high = 0.0f;

    set_two(&low, &high);
    *table[swap] = *table[1 - swap] + low;
    F moved = as_moved(a);
    alias = moved + load<F>(&x) * 0.0f + high * 0.0f;
}

static void last(const stage *, long, F r, F g, F b, F a)
{
    out = r[0] + g[0] + b[0] + a[0];
}

static void pos_stage(const stage *program, long n, F r, F g, F b, F a)
{
    stage_k(r, g, b, a);
    stage_fn next = program->fn;
    if (!n)
        next = last;
    return next(program, n - 1, r, g, b, a);
}

// ---- negative

static const int *saved;
static long saved_number;

NOINLINE static int churn(int k)
{
    volatile int pad[64];
    for (int i = 0; i < 64; i++)
        pad[i] = -1 - k;
    return pad[k & 63] + k + 1;
}
NOINLINE static int read_saved(int k)
{
    int a = churn(k), b = churn(k);
    return *saved + a + b;
}
NOINLINE static int read_number(int k)
{
    int a = churn(k), b = churn(k);
    return *reinterpret_cast<const int *>(saved_number) + a + b;
}

SI void store_inline(const int &x) { saved = &x; }
static inline void store_plain(const int &x) { saved = &x; }
NOINLINE static void store_opaque(const int &x) { saved = &x; }
static inline void store_deeper(const int &x) { store_plain(x); }
static inline const int &give_back(const int &x) { return x; }
static inline const int *give_pointer(const int *x) { return x + 0; }
static inline void store_number(const int &x)
{
    saved_number = reinterpret_cast<long>(&x);
}
static inline void store_alias(const int &x)
{
    const int &y = x;
    const int *z = &y;
    saved = z;
}
static inline void store_table(const int &x)
{
    const int *table[] = {&x};
    saved = table[0];
}
static inline void store_lambda(const int &x)
{
    auto f = [&x] { saved = &x; };
    f();
}
struct holder {
    const int *p;
};
static holder held;
static inline void store_object(const int &x)
{
    holder h{&x};
    held = h;
    saved = held.p;
}
static inline void store_asm(const int &x)
{
#if defined(__x86_64__)
    asm volatile("movq %1, %0" : "=m"(saved) : "r"(&x));
#elif defined(__aarch64__)
    asm volatile("str %1, %0" : "=m"(saved) : "r"(&x));
#else
    saved = &x;
#endif
}
static inline void store_bytes(const int *x) { memcpy(&saved, &x, sizeof x); }
static void (*indirect)(const int &) = store_opaque;
static inline void store_indirect(const int &x) { indirect(x); }
__attribute__((weak)) void store_weak(const int &x) { saved = &x; }
static inline void store_recursive(const int &x, int n)
{
    if (n)
        return store_recursive(x, n - 1);
    saved = &x;
}
struct self {
    int v;
    void keep() const { saved = &v; }
};
struct registers {
    int v;
    registers(int x) : v(x) { saved = &v; }
};
static inline void store_conditional(const int &x, const int &y, int which)
{
    saved = which ? &x : &y;
}
static inline void store_laundered(const int &x)
{
    saved = __builtin_launder(&x);
}

#define NEG(name, ...)                                                          \
    NOINLINE static int neg_##name(int v)                                      \
    {                                                                          \
        int x = v;                                                             \
        __VA_ARGS__;                                                           \
        return read_saved(1);                                                  \
    }
NEG(inline, store_inline(x))
NEG(plain, store_plain(x))
NEG(opaque, store_opaque(x))
NEG(deeper, store_deeper(x))
NEG(returned, saved = &give_back(x))
NEG(returned_pointer, saved = give_pointer(&x))
NEG(alias, store_alias(x))
NEG(table, store_table(x))
NEG(lambda, store_lambda(x))
NEG(object, store_object(x))
NEG(asm, store_asm(x))
NEG(bytes, store_bytes(&x))
NEG(indirect, store_indirect(x))
NEG(weak, store_weak(x))
NEG(recursive, store_recursive(x, 2))
NEG(conditional, store_conditional(x, x, v & 1))
NEG(laundered, store_laundered(x))

NOINLINE static int neg_number(int v)
{
    int x = v;
    store_number(x);
    return read_number(1);
}
NOINLINE static int neg_member(int v)
{
    self s{v};
    s.keep();
    return read_saved(1);
}

NOINLINE static int neg_constructor(int v)
{
    registers r(v);
    return read_saved(1);
}

int main()
{
    static const stage program = {pos_stage};
    F r = {1, 1, 1, 1}, g = {2, 2, 2, 2}, b = {3, 3, 3, 3}, a = {4, 4, 4, 4};

    pos_stage(&program, depth, r, g, b, a);
    printf("%d\n", out > 0.0f);
    printf("%d %d %d %d %d\n", neg_inline(10), neg_plain(20), neg_opaque(30),
           neg_deeper(40), neg_returned(50));
    printf("%d %d %d %d %d\n", neg_returned_pointer(60), neg_alias(70),
           neg_table(80), neg_lambda(90), neg_object(100));
    printf("%d %d %d %d %d\n", neg_asm(110), neg_bytes(120), neg_indirect(130),
           neg_weak(140), neg_recursive(150));
    printf("%d %d %d %d %d\n", neg_conditional(160), neg_laundered(170),
           neg_number(180), neg_member(190), neg_constructor(200));
}
