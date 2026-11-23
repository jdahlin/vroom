/* [ext:gnu_attributes] noinline, always_inline and noreturn on a member
   function template (libc++'s basic_string::__assign_no_alias: _LIBCPP_NOINLINE) */
struct S {
  template <bool B> __attribute__((noinline)) int f(int x)
  {
    if (B)
      return x + 1;
    return x;
  }
  template <class T> __attribute__((always_inline)) inline T g(T x)
  {
    return x * 2;
  }
  template <class T> __attribute__((noreturn)) void die(T code)
  {
    __builtin_trap();
  }
};

int main()
{
  S s;
  if (s.f<true>(1) != 2 || s.f<false>(5) != 5)
    return 1;
  if (s.g(3) != 6 || s.g(2.5) != 5.0)
    return 2;
  return 0;
}
