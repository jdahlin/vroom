/* [basic.scope.enum]/1, [dcl.enum]/5: in a scoped enumeration's
   enumerator-list an initializer names an earlier enumerator unqualified,
   with the fixed underlying type (Skia's kLast = kLinear) */
enum class M : short { kN, kL, kLast = kL, kNext = kLast + 2 };
enum class N { a = 3, b = a * 2 };
static_assert((int)M::kLast == 1 && (int)M::kNext == 3 && (int)N::b == 6);
static constexpr int count = static_cast<int>(M::kLast) + 1;
int main() { return count + (int)N::b - 8; }
