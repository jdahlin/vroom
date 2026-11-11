/* A foldable VLA bound must still evaluate its side effects exactly once. */
static int calls[2];
static int touch(int which) { ++calls[which]; return 4; }
static int never(void) { __builtin_trap(); }
static int constant_short_circuit[1 || never()];

__attribute__((noinline)) static int check(int n) {
    int (*p)[(touch(0), 2)][(touch(1), 3)];
    if (calls[0] != n || calls[1] != n) return 1;
    if (sizeof(*p) != 6 * sizeof(int)) return 2;
    typedef int row[(touch(0), 2)];
    row a;
    a[0] = 7; a[1] = 9;
    if (sizeof(a) != 2 * sizeof(int) || a[0] + a[1] != 16) return 3;
    if (calls[0] != n + 1 || calls[1] != n) return 4;
    int b[(touch(1), 3)];
    b[0] = 11; b[2] = 13;
    if (sizeof(b) != 3 * sizeof(int) || b[0] + b[2] != 24) return 5;
    if (calls[0] != n + 1 || calls[1] != n + 1) return 6;
    int counter = 0;
    int (*q)[(++counter, 2)];
    if (counter != 1 || sizeof(*q) != 2 * sizeof(int)) return 7;
    int ordinary[1 || never()];
    if (sizeof(ordinary) != sizeof(int)) return 8;
    return constant_short_circuit[0];
}
int main(void) {
    int r = check(1);
    if (r) return r;
    return check(3);
}
