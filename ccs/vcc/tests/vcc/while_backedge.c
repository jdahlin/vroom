/* A terminal while body needs no second backedge; continue has its own branch. */
#define NI __attribute__((noinline))
NI int loop_return(int x)
{
    while (x) return x + 3;
    return 7;
}
NI int loop_break(int x)
{
    while (x) { x += 3; break; }
    return x;
}
NI int loop_continue(int x)
{
    int n = 0;
    while (x > 0) { --x; ++n; continue; }
    return n;
}
NI int loop_both(int x)
{
    while (x) {
        if (x > 0) return 1;
        else return -1;
    }
    return 0;
}
/* A nonterminal body retains its backedge. */
NI int loop_normal(int x)
{
    int n = 0;
    while (x > 0) { --x; ++n; }
    return n;
}
/* A label is not inferred to terminate: entering it may bypass earlier jumps. */
NI int loop_enter(int x)
{
    int n = 0;
    if (x == 0) goto inside;
    while (x > 0) {
        ++n;
inside:
        if (++x > 3) break;
    }
    return n * 10 + x;
}
NI int loop_goto(int x)
{
    while (x) goto out;
    return 3;
out:
    return 5;
}
static int cleaned;
static void cleanup(int *p) { cleaned += *p; }
NI int loop_cleanup(int x)
{
    while (x) {
        int n __attribute__((cleanup(cleanup))) = x;
        if (x > 0) return 11;
        else return 13;
    }
    return 17;
}
/* continue must still reach a do condition and a for increment. */
NI int loop_do(int x)
{
    int n = 0;
    do { ++n; continue; } while (--x > 0);
    return n;
}
NI int loop_for(int x)
{
    int n = 0;
    for (int i = 0; i < x; ++i) { ++n; continue; }
    return n;
}
int main(void)
{
    if (loop_return(5) != 8 || loop_return(0) != 7) return 1;
    if (loop_break(4) != 7 || loop_break(0) != 0) return 2;
    if (loop_continue(4) != 4 || loop_continue(0) != 0) return 3;
    if (loop_both(4) != 1 || loop_both(-4) != -1 || loop_both(0) != 0) return 4;
    if (loop_normal(4) != 4 || loop_normal(0) != 0) return 5;
    if (loop_enter(0) != 34 || loop_enter(1) != 34) return 6;
    if (loop_goto(0) != 3 || loop_goto(1) != 5) return 7;
    if (loop_cleanup(2) != 11 || cleaned != 2) return 8;
    if (loop_cleanup(-3) != 13 || cleaned != -1) return 9;
    if (loop_do(4) != 4 || loop_do(0) != 1) return 10;
    if (loop_for(4) != 4 || loop_for(0) != 0) return 11;
    return 0;
}
