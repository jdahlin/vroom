/* More than 1024 objects with destructors in one function: the one live at the throw is destroyed,
   [except.ctor]/2, and the handler catches, [except.handle]/1.  (Here, not in run/: it needs the host's
   unwinder.) */
extern "C" int printf(const char *, ...);

static int live;

struct guard {
    guard() { ++live; }
    ~guard() { --live; }
};

__attribute__((noinline)) static void step(int &n, int stop)
{
    if (++n == stop)
        throw n;
}

#define S                                                                      \
    {                                                                          \
        guard g;                                                               \
        step(n, stop);                                                         \
    }
#define S8 S S S S S S S S
#define S64 S8 S8 S8 S8 S8 S8 S8 S8
#define S512 S64 S64 S64 S64 S64 S64 S64 S64

static int run(int stop)
{
    int n = 0;

    try {
        S512 S512 S512
    } catch (int v) {
        return v;
    }
    return 0;
}

int main()
{
    int const stops[] = {1, 1000, 1022, 1030, 1500};

    for (int stop : stops) {
        int got = run(stop);

        printf("%d %d %d\n", stop, got, live);
        if (got != stop || live != 0)
            return 1;
    }
    return 0;
}
