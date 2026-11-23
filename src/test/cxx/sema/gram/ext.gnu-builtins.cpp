// [ext:gnu_library_builtins] [ext:clang_builtin_functions] the compiler
// builtins libc++ uses: one function per line, each verdict compared with
// clang's.  A call with the arguments' types a builtin takes is well-formed,
// one with others is an error where clang's is.
typedef unsigned long size_t;
struct S {
    int m;
    int f(int);
};
void launder_ok(int *p) { (void)__builtin_launder(p); }
void launder_void(void *p) { (void)__builtin_launder(p); }
void launder_int(int x) { (void)__builtin_launder(x); }
void launder_function(void (*p)()) { (void)__builtin_launder(p); }
void bswapg_ok(unsigned x) { (void)__builtin_bswapg(x); }
void bswapg_float(float x) { (void)__builtin_bswapg(x); }
void bswapg_pointer(int *x) { (void)__builtin_bswapg(x); }
void addc_ok(unsigned a, unsigned b, unsigned *c) { (void)__builtin_addc(a, b, 0u, c); }
void addc_value(unsigned a, unsigned b) { (void)__builtin_addc(a, b, 0u, b); }
void addc_few(unsigned a, unsigned b) { (void)__builtin_addc(a, b, 0u); }
void invoke_function(int (*p)(int)) { (void)__builtin_invoke(p, 1); }
void invoke_none() { (void)__builtin_invoke(); }
void invoke_int(int x) { (void)__builtin_invoke(x); }
void invoke_member(S *s) { (void)__builtin_invoke(&S::f, s, 1); }
void invoke_data(S &s) { (void)__builtin_invoke(&S::m, s); }
void invoke_data_extra(S &s) { (void)__builtin_invoke(&S::m, s, 1); }
void invoke_member_missing(S &s) { (void)__builtin_invoke(&S::f, s); }
void invoke_member_object() { (void)__builtin_invoke(&S::f); }
void assume_aligned_ok(void *p) { (void)__builtin_assume_aligned(p, 16); }
void assume_aligned_value(int x) { (void)__builtin_assume_aligned(x, 16); }
void assume_aligned_few(void *p) { (void)__builtin_assume_aligned(p); }
void prefetch_value(int x) { __builtin_prefetch(x); }
void object_size_ok(void *p) { (void)__builtin_object_size(p, 0); }
void object_size_value(int x) { (void)__builtin_object_size(x, 0); }
void library_ok(char *p, const char *q) { (void)__builtin_strcmp(p, q); }
void library_value(int x) { (void)__builtin_strlen(x); }
void library_few(char *p) { (void)__builtin_memcmp(p, p); }
void bzero_ok(char *p) { __builtin_bzero(p, 4); }
void bzero_value(int x) { __builtin_bzero(x, 4); }
void trap_ok() { __builtin_verbose_trap("a", "b"); }
void trap_value(int x) { __builtin_verbose_trap(x, "b"); }
void trivially_ok() { static_assert(__is_trivially_constructible(int)); }
