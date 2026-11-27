/* [expr.const]/3: a constant-initialized const variable of enumeration type
   is usable in constant expressions. */
extern "C" int printf(const char *, ...);
enum E { a, b, c, n32 = c };
enum class S : char { x, y };
const E g = b;
static_assert(g != n32, "");
template <S s> int h() { return int(s); }
int main(){ const E ct = b; static_assert(ct != n32 && ct == b, ""); const S s = S::y; static_assert(s == S::y, ""); int v[ct + 1] = {}; switch (v[0]) { case ct: return 1; default: break; } const int *p = (const int *)&g; printf("%d %d %zu %d\n", h<s>(), ct, sizeof v / sizeof *v, *p); }
