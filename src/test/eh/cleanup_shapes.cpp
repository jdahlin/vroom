// The C++ half of cleanup_shapes.c.
extern "C" {
void cxx_throw(int k);
int shapes(int k);
extern int s_order[32], s_n;
}

void cxx_throw(int k) { throw k; }

static bool is(const int *want, int n)
{
    if (s_n != n)
        return false;
    for (int i = 0; i < n; i++)
        if (s_order[i] != want[i])
            return false;
    return true;
}

static int run(int k, int expect_throw)
{
    s_n = 0;
    try {
        shapes(k);
        return expect_throw ? -1 : 0;
    } catch (int x) {
        return x == expect_throw ? 0 : -2;
    }
}

int main()
{
    static const int w0[] = {10, 11, 12, 13, 1}, w1[] = {1}, w2[] = {10, 11, 1},
                     w3[] = {10, 11, 12, 1}, w4[] = {10, 11, 12, 13, 1},
                     w5[] = {10, 11, 12, 13, 20, 1};

    if (run(0, 0) || !is(w0, 5))
        return 1;
    if (run(1, 1) || !is(w1, 1))
        return 2;
    if (run(2, 0) || !is(w2, 3))
        return 3;
    if (run(3, 0) || !is(w3, 4))
        return 4;
    if (run(4, 4) || !is(w4, 5))
        return 5;
    if (run(5, 6) || !is(w5, 6))
        return 6;
    return 0;
}
