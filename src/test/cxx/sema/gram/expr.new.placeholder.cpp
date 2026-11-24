// [expr.new]/2, [dcl.type.auto.deduct]/2: a placeholder in a new-expression's
// type is deduced from its single initializer expression.
int *g(double);
int i;
auto *p1 = new auto(1);
auto *p2 = new (auto (*)(double))(&g);
auto *p3 = new auto(&i);
auto *p4 = new auto *(&i);
static_assert(__is_same(decltype(p1), int *));
static_assert(__is_same(decltype(p2), int *(**)(double)));
static_assert(__is_same(decltype(p3), int **));
static_assert(__is_same(decltype(p4), int **));
