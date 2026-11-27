/* [temp.dep.candidate]: of a namespace's functions declared after a
   template, only argument-dependent lookup finds one; the earlier ones are
   the candidates of an instantiation's call */
extern "C" int printf(const char *, ...);

namespace opts {
static int pick(int, long, long) { return 1; }
static int pick(int, int, int) { return 2; }
static int one(long) { return 3; }

template <typename T> static int call_three(T t) { return pick(0, t, t); }
template <typename T> static int call_one(T t) { return one(t); }

/* better matches, but declared after the templates */
static int pick(int, float, float) { return 10; }
static int pick(int, short, short) { return 11; }
static int one(int) { return 12; }

static int direct() { return pick(0, 1.0f, 1.0f) * 100 + one(1); }
} // namespace opts

/* an associated namespace without later declarations is unaffected */
namespace other {
struct s {};
static int pick(s) { return 20; }
} // namespace other

int main()
{
    printf("%d %d\n", opts::call_three(1), opts::call_three(2L));
    printf("%d %d\n", opts::call_three(short(1)), opts::call_one(1));
    printf("%d\n", opts::direct());
    return 0;
}
