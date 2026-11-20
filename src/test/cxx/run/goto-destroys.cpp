/* [stmt.dcl]/2: "Upon each transfer of control ... within a function from
   a point P to a point Q, each variable with automatic storage duration
   that is active at P and not at Q is destroyed": a goto out of blocks, a
   goto back to before a declaration in the same block (a loop by goto),
   a goto that leaves nothing, and a temporary bound to a reference. */
extern "C" int printf(const char *, ...);

struct T {
    int v;
    T(int v) : v(v) { printf("T(%d)\n", v); }
    ~T() { printf("~T(%d)\n", v); }
};

int out_of_blocks(int x)
{
    T a(1);
    {
        T b(2);
        {
            T c(3);
            if (x)
                goto done;
            printf("not taken\n");
        }
        printf("after c\n");
    }
done:
    printf("done %d\n", a.v);
    return a.v;
}

int backwards()
{
    int n = 0;
again:
    T t(10 + n);
    const T &r = T(20 + n);
    n++;
    if (n < 3)
        goto again;
    printf("leaving %d %d\n", t.v, r.v);
    return n;
}

int forward_nothing(int x)
{
    T a(30);
    if (x)
        goto skip;
    printf("x is 0\n");
skip:
    return a.v;
}

int main()
{
    int s = out_of_blocks(1) + out_of_blocks(0);
    s += backwards();
    s += forward_nothing(1) + forward_nothing(0);
    return s - 65;
}
