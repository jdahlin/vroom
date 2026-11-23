/* [basic.scope.enum]/1, [dcl.enum]/5: in a scoped enumeration's
   enumerator-list an initializer names an earlier enumerator unqualified,
   with the fixed underlying type (Skia's kLast = kLinear) */
enum class M : short { kN, kL, kLast = kL, kNext = kLast + 2 };
enum class N { a = 3, b = a * 2 };
static_assert((int)M::kLast == 1 && (int)M::kNext == 3 && (int)N::b == 6);
static constexpr int count = static_cast<int>(M::kLast) + 1;
// An earlier enumerator hides an enclosing name, while other enclosing
// names remain visible in the initializer.
enum { Linear = 7, Offset = 2 };
enum class FilterMode : unsigned char {
    Nearest,
    Linear,
    Last = Linear,
    WithOffset = Last + Offset,
};
static_assert(FilterMode::Last == FilterMode::Linear);
static_assert(static_cast<int>(FilterMode::WithOffset) == 3);
static_assert(Linear == 7);

int main() { return count + (int)N::b - 8; }
