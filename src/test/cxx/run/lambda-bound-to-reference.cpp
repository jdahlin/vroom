/* [dcl.init.ref]/5.3.2, [class.temporary]/6: a lambda's closure prvalue
   bound to a forwarding reference, a const lvalue reference, an rvalue
   reference, with and without captures, as an argument and as a variable
   (libc++'s __atomic_wait_unless passes its poll lambda this way) */
template <class P> int call(P&& p)
{
    return p();
}
template <class P> int call_const(const P& p)
{
    return p();
}
int main()
{
    int x = 5;
    int total = 0;
    total += call([&]() { return x + 1; });
    total += call_const([x]() { return x + 2; });
    total += call([]() { return 3; });
    auto &&kept = [x, &total]() { return x * 10 + total; };
    const auto &keptc = [x]() { return x * 100; };
    total += kept() + keptc();
    return total == 6 + 7 + 3 + (50 + 16) + 500 ? 0 : 1;
}
