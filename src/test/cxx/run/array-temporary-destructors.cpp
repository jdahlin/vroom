/* [class.temporary]/4 and /6: array temporaries whose elements have
   destructors, as call arguments (at namespace scope too) and extended by a
   reference */
extern "C" int printf(const char *, ...);
static int log_[64], n_;
struct sp {
    int v;
    sp() : v(0) {}
    sp(int x) : v(x) { log_[n_++] = x; }
    sp(const sp &o) : v(o.v) { log_[n_++] = 100 + v; }
    sp(sp &&o) : v(o.v) { o.v = 0; log_[n_++] = 200 + v; }
    ~sp() { log_[n_++] = -v; }
};
struct arr { sp e[3]; };
template <class T, int N> int sum(T (&&a)[N]) { int s = 0; for (int i = 0; i < N; i++) s += a[i].v; return s; }
template <int N> arr take(sp (&&a)[N]) { arr r; for (int i = 0; i < N; i++) r.e[i].v = a[i].v; return r; }
template <class T, int N> int sumc(const T (&a)[N]) { int s = 0; for (int i = 0; i < N; i++) s += a[i].v; return s; }
struct st { int v; sp s; };
static void mark() { log_[n_++] = 999; }
static int total = sum<sp>({1, 2}) ;
static arr global_arr = take<3>({sp(3), 4, sp()});
static arr styles = take<2>({sp{5}, sp{6}});
static int local()
{
    static const arr a = take<2>({7, sp(8)});
    return a.e[0].v * 10 + a.e[1].v;
}
int main()
{
    mark();
    int x = sum<sp>({1, 2, 3}) * 100; mark();
    x += sumc<sp>({4, 5}); mark();
    arr r = take<3>({sp(1), 2, sp(3)}); mark();
    {
        const sp (&kept)[2] = {7, 8};
        mark();
        x += kept[1].v;
    }
    mark();
    x += local() + local(); mark();
    if (x > 0 && sum<sp>({9}) > 0) mark();
    if (x < 0 && sum<sp>({9}) > 0) mark();
    mark();
    for (int i = 0; i < n_; i++) printf("%d ", log_[i]);
    printf("| %d %d %d %d\n", x, total, global_arr.e[1].v, styles.e[1].v + r.e[2].v);
}
