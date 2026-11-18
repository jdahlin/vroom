/* cxx-run-flags: -std=c++17 */
/* [expr.static.cast]/4: an implicit conversion sequence from a noexcept
   function (or a pointer to one) to "pointer to function": the
   function-to-pointer conversion, then the function pointer conversion
   ([conv.fctptr]) */
int twice(int x) noexcept { return 2 * x; }
int main()
{
    auto p = static_cast<int (*)(int)>(twice);
    int (*q)(int) noexcept = twice;
    auto r = static_cast<int (*)(int)>(q);
    auto s = static_cast<int (*)(int)>(&twice);
    auto t = (int (*)(int))twice;
    if (p(1) + r(2) + s(3) + t(4) != 20)
        __builtin_abort();
    return 0;
}
