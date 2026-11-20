/* [expr.type.conv]/1-2: T(e), T() and T(e) through a typedef-name, for an
   enumeration type T: the cast expression of [expr.static.cast]/10 and
   the value-initialization of [dcl.init]/8 (libc++ <__charconv>) */
enum class F { a = 1, b = 2 };
enum U : unsigned char { x = 1 };
enum V { v1 = 5 };
constexpr F operator&(F p, F q)
{
    return F(static_cast<unsigned>(p) & static_cast<unsigned>(q));
}
constexpr F operator~(F p) { return F(~static_cast<unsigned>(p)); }
using FF = F;
static_assert(F(3) == F(3));
static_assert(unsigned(F(7)) == 7);
static_assert(unsigned(F()) == 0);
static_assert(U(257) == U(1));
static_assert(V(5) == v1);
static_assert(V() == 0);
static_assert((F::a & F::b) == F(0));
static_assert(FF(2) == F::b);
static_assert(unsigned(~F::a) == ~1u);
int main()
{
    int n = 2;
    return static_cast<int>(V(n)) - 2 + static_cast<int>(F(n)) - 2;
}
