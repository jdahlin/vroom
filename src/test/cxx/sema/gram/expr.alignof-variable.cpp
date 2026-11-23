// [expr.alignof] with the GNU extension of an expression operand: a variable's own
// alignment (alignas, aligned), not its type's
alignas(8) char c;
char b __attribute__((aligned(4)));
static_assert(alignof(c) == 8, "");
static_assert(__alignof__(b) == 4, "");
static_assert(alignof(char) == 1, "");
void f() { alignas(16) int x; static_assert(alignof(x) == 16, ""); }
