struct member {
    int v;
    member(int x) : v(x) {}
    member(const member &o) : v(o.v + 1) {}
};

struct box {
    member m;
    box(int x) : m(x) {}
    box(const box &) = default;
    ~box() = default;
    box &operator=(const box &) = default;
};

box make(int x) {
    box b(x);
    return b;
}

int main() {
    box r = make(41);
    return r.m.v == 42 ? 0 : 1;
}
