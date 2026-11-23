/* clang's type transformations, as libc++'s <type_traits> spells
   remove_cv_t, decay_t, add_pointer_t and the others: on known types and,
   in templates, on dependent ones (analysed again at instantiation).  Each
   result picks an overload of which(), whose answer says the type, or
   is used where only the right type works. */
extern "C" int printf(const char *, ...);

int which(int) { return 1; }
int which(int *) { return 2; }
int which(const int *) { return 3; }
int which(long) { return 6; }

template <class T> struct remove_cv {
    using type = __remove_cv(T);
};
template <class T> using remove_reference_t = __remove_reference_t(T);
template <class T> using decay_t = __decay(T);

int twice(int x) { return 2 * x; }

int main()
{
    int n = 4, arr[3] = {1, 2, 3}, grid[2][3] = {{0}};
    const int c = 5;
    remove_cv<const int>::type a = 7;
    a++; /* not const */
    __remove_cvref(const int &) b = 1;
    b++;
    __remove_const(const volatile int) v = 2;
    __remove_volatile(volatile int) w = 3;
    remove_reference_t<long &&> l = 8;
    __add_pointer(int &) p = &n;
    __add_pointer(const int) q = &c;
    __add_lvalue_reference(int &&) r = n;
    __add_rvalue_reference(int &) s = n; /* collapses to int & */
    __add_rvalue_reference(int) t = static_cast<int &&>(n);
    __remove_pointer(int *const) u = 9;
    __remove_extent(int[2][3]) *row = grid;
    __remove_all_extents(int[2][3]) e = 10;
    decay_t<int[3]> d = arr;
    decay_t<int(int)> f = twice;
    decay_t<const int &> g = 11;
    r++;
    s++;
    t++;
    printf("%d %d %d %d %d %d %d %d %d\n", which(a), which(b), which(p),
           which(q), which(d), (int)sizeof(*row), which(l), which(u),
           which(e));
    printf("%d %d %d %d %d %d %d %d\n", a, b, v + w, n, *d, f(g),
           (int)sizeof(__remove_extent(int[2][3])),
           (int)sizeof(__decay(int[3])));
    return which(g) == 1 && sizeof(__remove_all_extents(long[4][5])) == 8 ? 0
                                                                          : 1;
}
