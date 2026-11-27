/* [class.conv.fct], [temp.mem]: a conversion function declared after a
   conversion function template (SkPngCodec::voidp). */
extern "C" int printf(const char *, ...);
struct voidp {
    voidp(void *p) : f(p) {}
    template <typename T> operator T *() const { return (T *)f; }
    explicit operator bool() const { return f != nullptr; }
    operator long() const = delete;
    void *f;
};
struct other {
    explicit operator bool() const { return true; }
    template <typename T> operator T *() const { return nullptr; }
};
int main() {
    int x = 4;
    voidp v(&x), n(nullptr);
    int *p = v;
    char *q = static_cast<char *>(v);
    other o;
    int *z = o;
    printf("%d %d %d %d %d %d\n", *p, !!v, !!n, q != nullptr, z == nullptr, o ? 1 : 0);
}
