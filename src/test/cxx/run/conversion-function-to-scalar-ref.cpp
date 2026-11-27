using Fn = void (*)(int *);

struct Holder {
    Fn fn;
    int which;

    Holder(int *, Fn const &f) : fn(f), which(1) {}
    Holder(int *, Fn &&f) : fn(f), which(2) {}
};

struct Owner {
    Holder h{nullptr, [](int *p) { *p += 10; }};
};

struct ToInt {
    int v;
    operator int() const { return v; }
};

static int pick_int(int const &x) { return x + 100; }
static int pick_int(int &&x) { return x + 200; }

int main()
{
    Owner o;
    if (o.h.which != 2)
        return 1;
    int x = 5;
    o.h.fn(&x);
    if (x != 15)
        return 2;

    ToInt t{7};
    if (pick_int(t) != 207)
        return 3;

    return 0;
}
