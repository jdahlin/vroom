// objects and linkage: [basic.link], [dcl.init.aggr], [dcl.init.string],
// [stmt.dcl] static locals, [dcl.inline], function pointers
int global_counter = 2;
const int table[] = {5, 7, 11, 13};
static int hidden = 9;
const char message[] = "lowered";

static int next_id() {
    static int id = 100;
    return id++;
}

inline int square(int x) { return x * x; }

int apply(int (*op)(int), int v) { return op(v); }

int twice(int v) { return v * 2; }

int length(const char *s) {
    int n = 0;
    while (s[n])
        n++;
    return n;
}

int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

int main() {
    int grid[2][3] = {{1, 2, 3}, {4, 5}};
    int total = 0;
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 3; c++)
            total += grid[r][c] * (r + 1);
    next_id();
    int id = next_id();
    int (*f)(int) = twice;
    total += apply(square, 3) + apply(f, 4) + (*f)(1) + id - 100;
    total += table[3] + global_counter + hidden + length(message) +
             length("abc") + fib(10);
    global_counter = total;
    return global_counter & 255;
}
