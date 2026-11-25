/* diag-mode: -c */
/* diag-flags: --target=x86_64-linux-gnu */
/* -Wpsabi: once for a definition's result and once for its first wide
   parameter, once per direct call, at the callee. */
typedef int v8 __attribute__((vector_size(32)));
typedef double d8 __attribute__((vector_size(64)));
typedef int v4 __attribute__((vector_size(16)));
struct wrapped { v8 v; };

v8 external(v8, v8);
v8 result_only(void);
void variadic(int, ...);
extern void (*pointer)(v8);

v8 identity(v8 a) { return a; }
__attribute__((weak)) v8 two_parameters(v8 a, v8 b) { return a; }
void first_wide_parameter(int x, v8 a, d8 b) {}
d8 wide_result(void) { d8 x = {0}; return x; }

void calls(v8 *p)
{
    *p = external(*p, *p);
    *p = external(*p, *p);
    *p = result_only();
    variadic(1, *p);
    pointer(*p);
}

/* nothing: narrow, wrapped in a record, internal, always_inline */
v4 narrow(v4 a) { return a; }
void record(struct wrapped w) {}
static v8 internal(v8 a) { return a; }
void calls_internal(v8 *p) { *p = internal(*p); }
__attribute__((always_inline)) v8 forced(v8 a);
void calls_forced(v8 *p) { *p = forced(*p); }

/* a function emitted because it is used: its calls are checked */
static void helper(v8 *p) { result_only(); *p = external(*p, *p); }
static void unused(v8 *p) { *p = external(*p, *p); }
void (*table[])(v8 *) = {helper};

#pragma GCC diagnostic ignored "-Wpsabi"
v8 ignored(v8 a) { return a; }
