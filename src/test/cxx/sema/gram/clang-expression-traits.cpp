// __is_lvalue_expr and __is_rvalue_expr ([basic.lval], [ext:clang_type_traits]): clang's rvalue is a prvalue, an xvalue is neither; values are clang++'s.
struct S { int m; static int sm; int f(); };
int i;
int &lref();
int &&rref();
int val();
static_assert(__is_lvalue_expr(i) && !__is_rvalue_expr(i), "");
static_assert(__is_lvalue_expr((i)), "");
static_assert(__is_lvalue_expr(lref()) && !__is_rvalue_expr(lref()), "");
static_assert(!__is_lvalue_expr(rref()) && !__is_rvalue_expr(rref()), "");
static_assert(__is_rvalue_expr(val()) && !__is_lvalue_expr(val()), "");
static_assert(__is_rvalue_expr(1) && !__is_lvalue_expr(1), "");
static_assert(__is_rvalue_expr(i + 1), "");
static_assert(__is_lvalue_expr(S::sm), "");
static_assert(__is_lvalue_expr(*&i), "");
static_assert(__is_rvalue_expr(&i), "");
static_assert(__is_lvalue_expr("a"), "");
