// abs is computed in place for the C library's and the __builtin_ names; a
// program's own abs of another type or linkage is called.
extern "C" int printf(const char *, ...);
extern "C" int abs(int);
extern "C" long labs(long);
namespace std { using ::abs; using ::labs; }
namespace own { static int abs(int x) { return x + 1000; } }
static int calls;
static int next() { return -++calls; }
static int global = std::abs(-3);
__attribute__((noinline)) static long values(int a, long b)
{
    return std::abs(a) + __builtin_abs(a - 1) * 3 + std::labs(b) * 5 +
           __builtin_llabs(b) * 7 + own::abs(a) * 11;
}
int main()
{
    for (int i = -3; i <= 3; i++)
        printf("%ld\n", values(i, i * 100000L));
    int x = std::abs(next()) + __builtin_abs(next());
    printf("%d %d %d\n", x, calls, global);
}
