/* [dcl.enum]/5, [expr.const]/10: an enumerator of a fixed underlying type
   may be initialized by a bool, an integral promotion ([conv.prom]/6)
   (Skia's SkResolveConvexity: kNo = false, kYes = true), and a bool
   underlying type by 0 or 1 (P1401R5, as clang) */
enum class Resolve { kNo = false, kYes = true };
enum class Flag : bool { off = 0, on = 1 };
enum Small : unsigned char { s0 = false, s1 = true, s2 };
int main() { return (int)Resolve::kNo + ((int)Resolve::kYes != 1) * 2 + (s2 != 2) * 4 + ((bool)Flag::on != true) * 8; }
