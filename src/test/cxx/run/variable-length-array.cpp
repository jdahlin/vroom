/* [ext:clang_vla] variable length arrays (clang's and GCC's extension in
   C++): an automatic array whose bound is a run-time value, evaluated once
   where it is declared; sizeof reads it at run time; the stack is given
   back when the scope ends, however it is left */
typedef unsigned long uintptr;
/* clang -O2 may alternate between two places; a leak shows as a distance
   growing with the iterations */
static bool near(uintptr a, uintptr b)
{
    if (a < b)
        return b - a < 1024;
    return a - b < 1024;
}
struct P {
    int a, b;
};
static int calls;
static int bound(int n)
{
    calls++;
    return n;
}
static int sum(int *p, int n)
{
    int s = 0;
    for (int i = 0; i < n; i++)
        s += p[i];
    return s;
}
static void fill(char *p, unsigned long n, char v)
{
    for (unsigned long i = 0; i < n; i++)
        p[i] = v;
}
template <class T> T total(int n)
{
    T a[n];
    for (int i = 0; i < n; i++)
        a[i] = T(i + 1);
    T s = 0;
    for (int i = 0; i < n; i++)
        s += a[i];
    return s;
}
struct Holder {
    int base;
    int run(int n) const
    {
        int a[n];
        for (int i = 0; i < n; i++)
            a[i] = base + i;
        return sum(a, n);
    }
};
static int through_loop(int n, int rounds)
{
    /* the stack does not grow per iteration */
    uintptr first = 0;
    for (int r = 0; r < rounds; r++) {
        int a[n];
        a[0] = r;
        if (!r)
            first = reinterpret_cast<uintptr>(&a[0]);
        else if (!near(first, reinterpret_cast<uintptr>(&a[0])))
            return 1;
    }
    return 0;
}
static int leave_by_goto(int n)
{
    uintptr first = 0;
    for (int r = 0; r < 100; r++) {
        {
            char a[n + 16];
            fill(a, sizeof a, 1);
            if (!r)
                first = reinterpret_cast<uintptr>(a);
            else if (!near(first, reinterpret_cast<uintptr>(a)))
                return 1;
            if (r < 99)
                goto next;
            return a[n];
        }
    next:;
    }
    return 2;
}
static int leave_by_break_continue(int n)
{
    int count = 0;
    for (int r = 0; r < 50; r++) {
        int a[n + r];
        a[0] = r;
        if (r % 3 == 0)
            continue;
        if (r == 40)
            break;
        count += a[0] & 1;
    }
    return count;
}
static int in_lambda(int n)
{
    /* a variable length array of the lambda's own body */
    auto f = [](int k) {
        int a[k];
        for (int i = 0; i < k; i++)
            a[i] = i;
        return sum(a, k) + int(sizeof a);
    };
    return f(n);
}
static int aligned(int n)
{
    char pad[n];
    int a[n] __attribute__((aligned(64)));
    pad[0] = 1;
    a[0] = pad[0];
    if (reinterpret_cast<uintptr>(a) % 64)
        return 100;
    return a[0];
}
int main()
{
    int n = 5;
    {
        int a[n];
        for (int i = 0; i < n; i++)
            a[i] = i + 1;
        if (sum(a, n) != 15)
            return 1;
        if (sizeof(a) != 5 * sizeof(int))
            return 2;
        if (sizeof a / sizeof a[0] != 5)
            return 3;
        if (sizeof(a[0]) != sizeof(int))
            return 4;
        if (&a[3] - a != 3 || *(a + 2) != 3)
            return 5;
        int *p = a;
        p[1] = 20;
        if (a[1] != 20)
            return 6;
    }
    /* the bound is evaluated once, where the array is declared */
    {
        int m = 3;
        int a[bound(m)];
        m = 100;
        a[2] = 1;
        if (calls != 1 || sizeof(a) != 3 * sizeof(int))
            return 7;
    }
    /* element types */
    {
        char c[n * 3];
        P p[n];
        long long l[n];
        double d[n];
        for (int i = 0; i < n; i++)
            p[i].a = i, p[i].b = 2 * i;
        if (sizeof c != 15 || sizeof p != 5 * sizeof(P) ||
            sizeof l != 40 || sizeof d != 40)
            return 8;
        if (p[4].a + p[4].b != 12)
            return 9;
    }
    /* any integral bound; zero */
    {
        unsigned char u = 4;
        bool b = true;
        long long w = 6;
        enum E { two = 2 } e = two;
        int a[u];
        int c[b];
        int d[w];
        int f[e];
        int z[n - 5];
        if (sizeof a != 16 || sizeof c != 4 || sizeof d != 24 || sizeof f != 8)
            return 10;
        (void)z;
    }
    /* a bound that is not a constant expression though const */
    {
        const int c = n + 1;
        int a[c];
        if (sizeof a != 6 * sizeof(int))
            return 11;
    }
    /* the first dimension is run time */
    {
        int m[n][3];
        for (int i = 0; i < n; i++)
            for (int j = 0; j < 3; j++)
                m[i][j] = i * 10 + j;
        if (m[4][2] != 42 || sizeof m != 5 * 3 * sizeof(int) ||
            sizeof m[0] != 3 * sizeof(int))
            return 12;
        int(*row)[3] = m + 2;
        if ((*row)[1] != 21)
            return 13;
    }
    if (through_loop(7, 1000))
        return 14;
    if (leave_by_goto(10))
        return 15;
    if (leave_by_break_continue(4) != 13)
        return 16;
    if (total<int>(10) != 55 || total<double>(4) != 10.0)
        return 17;
    Holder h = {10};
    if (h.run(4) != 46)
        return 18;
    /* nested scopes, two at a time, sizes read after the inner ends */
    {
        int a[n];
        a[0] = 1;
        {
            int b[n * 2];
            b[0] = 2;
            if (sizeof b != 2 * sizeof a)
                return 19;
            a[1] = b[0];
        }
        {
            long c[n + 3];
            c[0] = 4;
            a[2] = c[0];
        }
        if (a[0] + a[1] + a[2] != 7)
            return 20;
    }
    if (in_lambda(4) != 6 + 16)
        return 21;
    if (aligned(3) != 1)
        return 22;
    return 0;
}
