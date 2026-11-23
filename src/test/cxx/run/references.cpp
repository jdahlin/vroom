// references: [dcl.ref], [dcl.init.ref], [expr.cond], [expr.comma]
static int store[4] = {3, 1, 4, 1};

int &at(int i) { return store[i]; }

void bump(int &x, int by) { x += by; }

int sum(const int &a, const int &b) { return a + b; }

int pick(bool first, int &a, int &b) {
    (first ? a : b) = 42;
    int &c = (a, b);
    c += 1;
    return a + b;
}

int main() {
    int v = 5;
    int &r = v;
    r = 7;
    bump(v, 3);
    bump(at(2), 10);
    at(0)++;
    const int &t = v * 2;
    int x = 1, y = 2;
    int p = pick(true, x, y);
    int q = pick(false, x, y);
    return (v + t + sum(4, v) + store[0] + store[2] + p + q) & 255;
}
