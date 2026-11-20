/* a closure type as a template argument: a member of the class template
   specialization is weak (linkonce_odr) in clang, the closure type
   being no reason for internal linkage */
#define check(e) do { if (!(e)) __builtin_abort(); } while (0)

template <class F> struct holder {
    F f;
    int go() { return f(3); }
};

int main()
{
    holder<decltype([](int x) { return x; })> h;
    check(h.go() == 3);
    return 0;
}
