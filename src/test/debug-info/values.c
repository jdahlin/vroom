/* bench/debug-values.py: at the line marked STOP a debugger prints the same
   values from vcc's -g -O0 program as from clang's */
struct pair {
    int x;
    const char *name;
};
int counter = 41;
static double ratio = 2.5;
struct pair origin = {3, "o"};

__attribute__((noinline)) static int work(int a, long b, struct pair q, char c,
                                          double d)
{
    int local = a + 1;
    long list[3] = {b, b + 1, b + 2};
    struct pair copy = q;
    char big[200];

    big[0] = c;
    for (int i = 0; i < 2; i++)
        local += i;
    counter += local;
    return counter + (int)list[2] + copy.x + big[0] + (int)d; /* STOP */
}

int main(void)
{
    return work(10, 20, origin, 'A', ratio) == 0;
}
/* PRINT a b q.x c d local list copy.x counter ratio origin.x */
