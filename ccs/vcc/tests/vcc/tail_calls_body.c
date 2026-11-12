/* Sibling calls anywhere in a body (VIR at -O2: a call whose result the ret
   returns, also through a branch to the ret's block): the epilogue before the
   branch restores the callee-saved registers and the frame; what must stay a
   call does. */
#define NI __attribute__((noinline))
typedef long long I;
typedef unsigned long long U;

static volatile int sink;
NI int tb_add(int a, int b) { return a + b; }
NI I tb_wide(I a) { return a * 0x100000001LL; }
NI U tb_high(U a) { return a | 0xffffffff00000000ULL; }
NI int *tb_next(int *p) { return p + 1; }
NI int tb_read(const int *p) { return *p; }
NI void tb_void(int a) { sink += a; }
NI int tb_mix(int a, int b, int c, int d) { return a * 1000 + b * 100 + c * 10 + d; }

/* values live across calls (callee-saved registers), then a sibling call in one branch */
NI int tb_saved(int a, int b)
{
    int x = tb_add(a, 1), y = tb_add(b, 2), z = tb_add(x, y);

    if (a > 0)
        return tb_mix(x, y, z, a);
    return x + y + z;
}

/* a sibling call in each arm: the branch to the shared ret passes the result */
NI int tb_arms(int a, int b)
{
    int r;

    if (a & 1)
        r = tb_add(a, b);
    else
        r = tb_mix(a, b, a, b);
    return r;
}

/* through the function pointer, with an argument cycle */
NI int tb_ind(int (*fn)(int, int, int, int), int a, int b, int c, int d) { return fn(d, c, b, a); }

/* pointer and 64-bit results */
NI int *tb_ptr(int *p) { return tb_next(p); }
NI I tb_i64(I a) { return tb_wide(a + 3); }

/* not a sibling call: the result is widened from 32 bits (the upper half matters) */
NI I tb_zext(U a) { return (unsigned)tb_high(a); }
NI I tb_sext(U a) { return (int)tb_high(a); }

/* not a sibling call: the callee reads the caller's local */
NI int tb_local(int a)
{
    int v = a * 3;

    sink = tb_add(v, 1);
    return tb_read(&v);
}

/* not a sibling call: a local array whose element address the callee reads */
NI int tb_array(int a)
{
    int v[4] = { a, a + 1, a + 2, a + 3 };

    return tb_read(&v[2]);
}

/* a void call before a void return */
NI void tb_void2(int a, int b)
{
    if (a)
        tb_void(a + b);
    else
        tb_void(b);
}

int main(void)
{
    int arr[2] = { 5, 6 };

    if (tb_saved(3, 4) != 4 * 1000 + 6 * 100 + 10 * 10 + 3 || tb_saved(-3, 4) != -2 + 6 + 4) return 1;
    if (tb_arms(3, 4) != 7 || tb_arms(2, 5) != 2525) return 2;
    if (tb_ind(tb_mix, 1, 2, 3, 4) != 4321) return 3;
    if (tb_ptr(arr) != arr + 1 || tb_i64(1) != 0x400000004LL) return 4;
    if (tb_zext(5) != 5 || tb_sext(5) != 5 || tb_sext(0x80000000ULL) != -2147483648LL) return 5;
    if (tb_local(4) != 12 || sink != 13) return 6;
    if (tb_array(7) != 9) return 7;
    tb_void2(1, 2);
    tb_void2(0, 4);
    if (sink != 13 + 3 + 4) return 8;
    return 0;
}
