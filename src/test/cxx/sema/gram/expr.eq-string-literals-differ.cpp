// [expr.eq]/3.1, [lex.string]/9: the addresses of two string literals that
// cannot overlap (their characters differ where one is laid over the other)
// compare unequal, a constant expression.
static_assert("foo" + 0 != "bar", "");
static_assert((const char *)"foo" != "oo", "");
static_assert("xfoo" + 1 != "yfoo" + 1, "");
