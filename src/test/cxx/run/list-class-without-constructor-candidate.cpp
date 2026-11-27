/* [over.match.list]: a braced list no constructor of a tag class takes
   makes a candidate not viable */
extern "C" int printf(const char *, ...);
struct tag {
    explicit tag() = default;
};
struct view {
    int a;
    view(int x, int y) : a(x + y) {}
};
struct pr {
    int v;
    pr(const view &x, int y) : v(x.a * 10 + y) {}
    template <class U> pr(tag, const U &x, int y) : v(x.a + y) {}
};
static pr f() { return {{1, 2}, 3}; }
int main() { printf("%d\n", f().v); }
