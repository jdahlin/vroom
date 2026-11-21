// [class.union.anon]/1: an anonymous union member of class type whose
// special member functions are trivial (a user constructor taking
// arguments does not make them non-trivial).
struct Pair {
    int a, b;
    Pair() = default;
    Pair(int x) : a(x), b(x) {}
};
struct Holder {
    union {
        Pair p;
        int raw[2];
    };
};

int main() {
    Holder h;
    h.raw[0] = 1;
    h.raw[1] = 2;
    return h.p.b != 2;
}
