/* A function with more than 4096 calls and something to unwind: every call is in its exception table, so a
   handler catches and the destructors run, [except.handle]/1, [except.ctor]/2.  (Here, not in run/: it needs
   the host's unwinder.) */
extern "C" int printf(const char *, ...);

static int live, destroyed;

struct guard {
    guard() { ++live; }
    ~guard() { --live, ++destroyed; }
};

__attribute__((noinline)) static void step(int &n, int stop)
{
    if (++n == stop)
        throw n;
}

#define S step(n, stop);
#define S8 S S S S S S S S
#define S64 S8 S8 S8 S8 S8 S8 S8 S8
#define S512 S64 S64 S64 S64 S64 S64 S64 S64
#define S4096 S512 S512 S512 S512 S512 S512 S512 S512

static int run(int stop)
{
    int n = 0, got = 0;
    guard outer;

    try {
        guard inner;

        S4096 S512
    } catch (int v) {
        got = v;
    }
    return got;
}

int main()
{
    int const stops[] = {1, 4000, 4097, 4600};

    for (int stop : stops) {
        int got = run(stop);

        printf("%d %d %d %d\n", stop, got, live, destroyed);
        if (got != stop || live != 0)
            return 1;
    }
    return destroyed != 8;
}
