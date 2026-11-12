/* A compare whose flags an identical earlier compare left (VIR at -O2: the record
   clean-up drops it).  It must stay when the value changed in between on any
   way in, when another compare or a call came between, and at a join where the
   ways in compared different things. */
#define NI __attribute__((noinline))

static volatile int sink;
NI int id(int x) { return x; }

/* the same compare after a store: the flags still hold */
NI int same(int x, int *p)
{
    if (x >= 12)
        return id(1);
    *p = 3;
    if (x >= 12)
        return 7;
    if (x > 0)
        return id(x);
    return 0;
}

/* x changes on one way to the second compare */
NI int changed(int x, int y)
{
    if (x < 5)
        sink = 1;
    else
        x = y;
    if (x < 5)
        return 10;
    return 20;
}

/* a call between: the flags do not survive it */
NI int called(int x)
{
    int r = 0;

    if (x < 5)
        r = id(1);
    if (x < 5)
        return r + 30;
    return r + 40;
}

/* a join of two ways that compared different registers the same way */
NI int join(int x, int y, int c)
{
    if (c) {
        if (x < 5)
            sink = 2;
    } else {
        if (y < 5)
            sink = 3;
    }
    if (x < 5)
        return 50;
    return 60;
}

/* a loop: the compare at the top is reached from the bottom too */
NI int loop(int x, int n)
{
    int s = 0;

    for (int i = 0; i < n; i++) {
        if (x < 3)
            s += 1;
        x = sink + i;
        if (x < 3)
            s += 100;
    }
    return s;
}

int main(void)
{
    int v = 0;

    if (same(12, &v) != 1 || v || same(3, &v) != 3 || v != 3 || same(-1, &v) != 0) return 1;
    if (changed(1, 9) != 10 || changed(9, 1) != 10 || changed(9, 7) != 20) return 2;
    if (called(1) != 31 || called(7) != 40) return 3;
    if (join(1, 9, 0) != 50 || join(9, 1, 0) != 60 || join(1, 9, 1) != 50 || join(9, 1, 1) != 60) return 4;
    sink = 0;
    if (loop(0, 3) != 1 + 100 + 1 + 100 + 1 + 100) return 5;
    if (loop(9, 2) != 0 + 100 + 1 + 100) return 6;
    return 0;
}
