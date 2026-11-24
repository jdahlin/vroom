// [expr.ref]/6.1: the object expression of a static data member's name is
// evaluated and not read, so an inactive union member in it breaks no rule
// ([class.union]/2, [expr.const]/5.9)
struct S { int n; static constexpr int k = 42; };
union U { int n; S s; };
constexpr U u = {0};
static_assert(u.s.k == 42, "");
