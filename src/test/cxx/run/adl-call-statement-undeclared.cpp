// [basic.lookup.argdep], [stmt.ambig]: a statement f(x); of a hidden
// friend's name, which only argument-dependent lookup finds, is a call.
struct P {
    friend void bump(P &p) { p.x += 1; }
    int x = 1;
};
namespace n
{
struct Q {
    int y = 10;
    friend void add(Q &a, Q &b) { a.y += b.y; }
};
} // namespace n

static int run(P paint, n::Q q, n::Q r)
{
    if (paint.x) {
        bump(paint);
        add(q, r);
    }
    for (bump(paint); paint.x < 4;)
        bump(paint);
    return paint.x + q.y;
}

int main() { return run(P(), n::Q(), n::Q()) - 24; }
