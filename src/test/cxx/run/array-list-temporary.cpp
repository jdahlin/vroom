/* [dcl.init.list]/3, [class.temporary]: an array's or scalar's list
   temporary bound to a reference */
extern "C" int printf(const char *, ...);
template <class T, int N> struct array {
    T e[N];
    constexpr const T &operator[](int i) const { return e[i]; }
};
template <class T, int N> constexpr array<T, N> to_array(T (&&a)[N])
{
    array<T, N> r{};
    for (int i = 0; i < N; i++)
        r.e[i] = a[i];
    return r;
}
template <class T, int N> int sum(T (&&a)[N])
{
    int s = 0;
    for (int i = 0; i < N; i++)
        s += (int)a[i];
    return s;
}
typedef int (*proc)(int);
static int twice(int x) { return 2 * x; }
static int thrice(int x) { return 3 * x; }
static int twice_of(const int &x) { return 2 * x; }

static auto dynamic = to_array<int>({twice(4), 5});
static int total = sum<int>({twice(1), 2});

static int pick(int i)
{
    static constexpr auto table = to_array<int>({10, 20, 30});
    return table[i];
}

int main()
{
    const proc (&procs)[2] = {twice, thrice};
    const int (&kept)[2] = {7, 8};
    int x = 2;
    int a = sum<int>({1, x, 3});
    int b = twice_of(int{x + 1});
    printf("%d %d %d %d %d %d %d %d\n", a, b, procs[0](3), procs[1](3),
           dynamic[0], total, kept[1], pick(2));
    return !(a == 6 && b == 6 && procs[0](3) == 6 && procs[1](3) == 9 &&
             dynamic[0] == 8 && total == 4 && kept[1] == 8 && pick(2) == 30);
}
