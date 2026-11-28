/* [dcl.attr.grammar]/6, [ext:gnu_attributes]: [[gnu::target]] and
   [[gnu::noinline]] before a function's declaration and definition */
/* cxx-run-flags: -std=c++20 */
[[gnu::noinline]] static int twice(int x) { return 2 * x; }
[[gnu::noinline]] int declared(int x);
int declared(int x) { return x + 1; }
#if defined(__x86_64__)
[[gnu::target("sse4.1")]] static int first(int x)
{
    int __attribute__((vector_size(16))) v = {x, 2, 3, 4};
    return __builtin_ia32_vec_ext_v4si(v, 0);
}
#else
static int first(int x) { return x; }
#endif
int main()
{
    return twice(2) != 4 || declared(2) != 3 || first(9) != 9;
}
