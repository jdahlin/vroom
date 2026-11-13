/* clang (static data only): a static initializer may read a const,
 * non-volatile object with an initializer: an array element, a member, a
 * whole element struct (clk-zx297520v3.c's .parent = clk_main[0]).  Not an
 * ICE, and not through a pointer or a volatile object. */
struct pd { const char *name; int index; };
struct w { struct pd p; };
static const struct pd clk_main[] = { { .name = "osc26m", .index = -1 }, { "pll", 7 } };
static const struct w cwa[] = { { { "d", 4 } } };
static const int ci[] = { 5, 6 };
static const double dd[] = { 1.5, 2.5 };
static const char *const names[] = { "zero", "one" };
struct fd { const char *name; struct pd parent; int div; };
static const struct fd divs[] = { { "x", clk_main[0], 2 }, { "y", clk_main[1], ci[1] } };
static struct pd p = cwa[0].p;
static int sum = ci[0] + ci[1] + clk_main[1].index;
static double d = dd[1];
static const char *nm = names[1];

int f(void)
{
    const int la[] = { 10, 20 };
    static int y = la[1];
    return y;
}

int main(void)
{
    if (divs[0].parent.index != -1 || divs[0].parent.name[0] != 'o' || divs[1].parent.index != 7 || divs[1].div != 6)
        return 1;
    if (p.index != 4 || p.name[0] != 'd') return 2;
    if (sum != 18) return 3;
    if (d != 2.5) return 4;
    if (nm[0] != 'o' || nm[1] != 'n') return 5;
    if (f() != 20) return 6;
    return 0;
}
