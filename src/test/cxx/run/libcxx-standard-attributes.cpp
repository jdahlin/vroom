/* [dcl.attr.noreturn] [[noreturn]] before a declaration, as libc++'s
   __libcpp_verbose_abort (GCC's noreturn); [dcl.fct]/3 an attribute
   without effect before a parameter (libc++'s [[__maybe_unused__]]);
   clang's availability and no_sanitize("cfi") (_LIBCPP_NO_CFI), which
   change no code */
[[__noreturn__]] __attribute__((availability(macos, strict, introduced = 10.0)))
void stop(int);
[[gnu::noreturn]] void stop2(int);
__attribute__((__no_sanitize__("cfi"))) int add(int a, [[__maybe_unused__]] int b = 2)
{
    return a + 40;
}
int main()
{
    return add(2) - 42;
}
