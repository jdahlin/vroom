/* [dcl.ref]/1, [class.mem]/15: a non-static data member of reference type
   may refer to an incomplete type; the member is a pointer
   (parse.h's nesting_level holds a reference to its enclosing class) */
struct later;
struct holder {
    later &l;
    later &&r;
    int (&unbounded)[];
    explicit holder(later &a, later &&b, int (&c)[]) : l(a), r(static_cast<later &&>(b)), unbounded(c) {}
};
static_assert(sizeof(holder) == 3 * sizeof(void *), "");
struct later {
    int v;
};
struct outer {
    int count;
    struct guard {
        outer &o;
        explicit guard(outer &o) : o(o) { o.count++; }
        ~guard() { o.count--; }
    };
};
int main()
{
    outer o = {};
    int arr[2] = {1, 2};
    later x = {5};
    {
        outer::guard g(o);
        if (o.count != 1)
            return 1;
    }
    holder h(x, static_cast<later &&>(x), arr);
    return o.count + h.l.v - 5 + h.unbounded[1] - 2 + !(&h.r == &x);
}
