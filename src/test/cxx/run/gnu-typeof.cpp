/* [ext:gnu_typeof] typeof, __typeof and __typeof__ with an expression or a
   type-id: the type of the expression (no reference for an lvalue, unlike
   decltype), the type-id's own cv-qualifiers, in a template, as a cast's
   type (glibc's <math.h> iscanonical) and as a declaration's. */
template <class T, class U> struct same {
    static constexpr bool value = false;
};
template <class T> struct same<T, T> {
    static constexpr bool value = true;
};

int g = 3;
int &r = g;
const int c = 4;
int a[3] = {1, 2, 3};
int f(double);

static_assert(same<__typeof(g), int>::value, "");
static_assert(same<__typeof__(r), int>::value, "");
static_assert(same<__typeof(c), const int>::value, "");
static_assert(same<__typeof(a), int[3]>::value, "");
static_assert(same<__typeof(&g), int *>::value, "");
static_assert(same<__typeof(g + 1.5), double>::value, "");
static_assert(same<__typeof(f(1)), int>::value, "");
static_assert(same<__typeof(int *), int *>::value, "");
static_assert(same<__typeof(const char), const char>::value, "");
static_assert(same<__typeof(&f), int (*)(double)>::value, "");
static_assert(sizeof(__typeof(a)) == 3 * sizeof(int), "");

template <class T> int twice(T x)
{
    __typeof(x) y = x;
    return (int)(__typeof(x))(y + y);
}

int main()
{
    __typeof(g) x = 5;
    const __typeof(r) y = 6;
    __typeof(a) b = {7, 8, 9};
    __typeof(x) *p = &x;
    int total = x + y + b[2] + *p + (int)(__typeof(g))(c);
    (void)(__typeof(g))(g);
    if (total != 5 + 6 + 9 + 5 + 4 || twice(2) != 4)
        return 1;
    return 0;
}
