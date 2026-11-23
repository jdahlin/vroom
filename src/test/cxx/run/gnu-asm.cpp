// [ext:gnu_asm_statements] (GCC manual 6.47): asm statements in a function
// body, basic and extended: qualifiers, outputs, inputs, symbolic names,
// clobbers, operands that are lvalues of references and members, constants
extern "C" int printf(const char *, ...);

static int add(int a, int b)
{
    int r;
    asm("add %w0, %w1, %w2" : "=r"(r) : "r"(a), "r"(b));
    return r;
}

// symbolic names and the spellings of the keyword
static long scaled(long x, long y)
{
    long r;
    __asm__ __volatile__("add %[out], %[a], %[b], lsl #2"
                         : [out] "=&r"(r)
                         : [a] "r"(x), [b] "r"(y));
    return r;
}

// a read-write operand, several outputs, a clobber, a basic asm, "::"
static void swap_and_count(int &a, int &b, int &count)
{
    int t;
    asm volatile("mov %w0, %w1\n\t"
                 "mov %w1, %w2\n\t"
                 "mov %w2, %w0\n\t"
                 "add %w3, %w3, #1"
                 : "=&r"(t), "+r"(a), "+r"(b), "+r"(count)
                 :
                 : "cc");
    asm("nop");
    asm volatile("" ::: "memory");
    asm volatile("" :::);
    asm volatile("nop" ::);
}

struct pair {
    int first, second;
    int sum() const
    {
        int r;
        asm inline("add %w0, %w1, %w2"
                   : "=r"(r)
                   : "r"(first), "r"(second));
        return r;
    }
    void set(int v)
    {
        asm("mov %w0, %w1" : "=r"(first) : "r"(v));
    }
};

static int calls;
static int seen(int v)
{
    calls++;
    return v;
}

constexpr int shift = 3;

// a memory operand, a constant, an array that decays, a call as an operand
static int load_and_shift(const int *p)
{
    int r;
    asm("ldr %w0, %1" : "=r"(r) : "m"(*p));
    asm("lsl %w0, %w0, %1" : "+r"(r) : "i"(shift));
    return r;
}

static int first_of(int (&a)[4])
{
    int r;
    asm("ldr %w0, [%1]" : "=r"(r) : "r"(a) : "memory");
    return r;
}

int main()
{
    int x = 7, y = 9, n = 0;
    pair p{3, 4};
    int arr[4] = {42, 1, 2, 3};
    int ok = add(2, 3) == 5 && scaled(1, 2) == 9;
    swap_and_count(x, y, n);
    ok = ok && x == 9 && y == 7 && n == 1;
    ok = ok && p.sum() == 7;
    p.set(11);
    ok = ok && p.first == 11;
    int r;
    asm("add %w0, %w1, #1" : "=r"(r) : "r"(seen(4)));
    ok = ok && r == 5 && calls == 1;
    ok = ok && load_and_shift(&y) == 56 && first_of(arr) == 42;
    printf("asm %d\n", ok);
    return ok ? 0 : 1;
}
