// A constructor of a class with an anonymous struct member (libc++'s
// _LIBCPP_COMPRESSED_PAIR in __tree) that initializes none of the struct's
// members: they are default-initialized, the constructor being no
// constexpr one ([dcl.constexpr]/4 binds only those, and only for unions).
extern "C" int printf(const char *, ...);
struct E { int v; E() : v(7) {} };
struct T {
    int size;
    struct {
        E end;
        int pad;
    };
    explicit T(int n) : size(n) { pad = 1; }
};
int main() {
    T t(3);
    printf("%d %d %d\n", t.size, t.end.v, t.pad);
    return t.size + t.end.v + t.pad - 11;
}
