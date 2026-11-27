// [ext:clang_attributes] [[clang::musttail]] is a jump at every level: chains
// deep enough to overflow the stack if any link were an ordinary call
extern "C" int printf(const char *, ...);

#define TAIL [[clang::musttail]]

constexpr long depth = 3000000;

typedef float F __attribute__((vector_size(16)));

struct stage_list {
    void *fn;
};

using stage = void (*)(stage_list *program, long n, F r, F g, F b, F a);

static float out[4];

static void just_return(stage_list *, long, F r, F g, F b, F a)
{
    F s = r + g + b + a;
    for (int i = 0; i < 4; i++)
        out[i] = s[i];
}

__attribute__((always_inline)) static inline void add_k(F &r) { r += 1.0f; }

static void add(stage_list *program, long n, F r, F g, F b, F a)
{
    if (!n)
        TAIL return ((stage)program[1].fn)(program, n, r, g, b, a);
    add_k(r);
    auto fn = (stage)program->fn;
    TAIL return fn(program, n - 1, r, g, b, a);
}

template <int Step> static long count(long n, long sum)
{
    volatile char pad[100];
    pad[n & 63] = Step;
    if (n <= 0)
        return sum;
    TAIL return count<Step>(n - 1, sum + pad[n & 63]);
}

struct walker {
    long steps = 0;
    long run(long n)
    {
        if (!n)
            return steps;
        steps++;
        __attribute__((musttail)) return run(n - 1);
    }
    long forward(long n) { TAIL return this->run(n); }
    static int parity(int n)
    {
        if (n < 2)
            return n;
        TAIL return parity(n - 2);
    }
};

struct small {
    long a, b;
};

static small both(long n, small s)
{
    if (!n)
        return s;
    s.a++;
    s.b += 2;
    TAIL return both(n - 1, s);
}

static int &pick(long n, int &r)
{
    if (!n)
        return r;
    TAIL return pick(n - 1, r);
}

int main()
{
    stage_list program[2] = {{(void *)add}, {(void *)just_return}};
    F z = {0, 1, 2, 3};
    add(program, depth, z, z, z, z);
    printf("%g %g %g %g\n", out[0], out[1], out[2], out[3]);
    printf("%ld %ld\n", count<1>(depth, 0), count<3>(depth, 0));
    walker w;
    printf("%ld %d\n", w.forward(depth), walker::parity(depth + 1));
    small s = both(depth, small{0, 0});
    int v = 5;
    pick(depth, v) = 7;
    printf("%ld %ld %d\n", s.a, s.b, v);
}
