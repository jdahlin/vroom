/* [class.conv.ctor]: explicit constructors take part in
   direct-initialization only; a standard conversion
   sequence is better than a user-defined one ([over.ics.rank]/2) */
struct Exact {
    int v;
    explicit Exact(int x) : v(x) {}
    Exact(long x) : v(int(x) + 100) {}
};
struct Wrap {
    int v;
    Wrap(int x) : v(x) {}
};
static int take(Exact e) { return e.v; }
static int pick(int) { return 1; }
static int pick(Wrap) { return 2; }

int main()
{
    Exact direct(5);
    Exact copied = 7;
    if (direct.v != 5 || copied.v != 107)
        return 1;
    if (take(3) != 103)
        return 2;
    if (pick(4) != 1)
        return 4;
    return 0;
}
