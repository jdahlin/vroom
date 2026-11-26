/* A defaulted move constructor initializes each member from an xvalue, also a member whose selected
   constructor is trivial while its copy constructor is not ([class.copy.ctor]/14): nlohmann/json's
   iteration_proxy_value lost its iterator.  And an array of such members, and a base. */
extern "C" int printf(const char *, ...);

static int copies, moves;

struct J {
    int *o = nullptr;
    long n = 0;
    J() = default;
    J(J &&) = default;
    J(J const &x) : o(x.o), n(x.n) { ++copies; }
};

struct counted {
    int v = 7;
    counted() = default;
    counted(counted const &x) : v(x.v) { ++copies; }
    counted(counted &&x) : v(x.v) { ++moves; }
};

struct member {
    J anchor{};
    counted s{};
    member() = default;
    member(member const &) = default;
    member(member &&) = default;
};

struct array_member {
    J anchor[2];
    counted s{};
    array_member() = default;
    array_member(array_member &&) = default;
};

struct base : J {
    counted s{};
    base() = default;
    base(base &&) = default;
};

static int x = 5, y = 6;

static void show(J const &j, int const *o, long n)
{
    printf("%d %ld\n", j.o == o, j.n);
}

static int bad;

static void check(J const &j, int const *o, long n)
{
    show(j, o, n);
    if (j.o != o || j.n != n)
        bad++;
}

int main()
{
    {
        member f;
        f.anchor.o = &x, f.anchor.n = 3;
        member g(static_cast<member &&>(f));
        check(g.anchor, &x, 3);
    }
    {
        array_member f;
        f.anchor[0].o = &x, f.anchor[1].o = &y, f.anchor[1].n = 4;
        array_member g(static_cast<array_member &&>(f));
        check(g.anchor[0], &x, 0);
        check(g.anchor[1], &y, 4);
    }
    {
        base f;
        f.o = &y, f.n = 9;
        base g(static_cast<base &&>(f));
        check(g, &y, 9);
    }
    printf("%d %d %d\n", copies, moves, bad);
    return bad || copies;
}
