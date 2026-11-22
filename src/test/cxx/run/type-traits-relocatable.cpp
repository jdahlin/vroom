// [ext:clang_type_traits]: __is_trivially_relocatable (clang's own rule),
// __builtin_is_cpp_trivially_relocatable (P2786) and
// __is_trivially_equality_comparable, answered as clang does.
struct Trivial {};
struct UserDtr { ~UserDtr(); };
struct UserCopy { UserCopy(const UserCopy &); };
struct HasUserDtr { UserDtr u; };
struct WithConst { const int i; };
struct WithRef { int &r; };
struct Derived : Trivial { int x; };
struct VirtualBase : virtual Trivial {};
union Bits { int a; float b; };
enum Byte : unsigned char {};

#define BOTH(expr) static_assert(__is_trivially_relocatable expr && __builtin_is_cpp_trivially_relocatable expr)
#define NEITHER(expr) static_assert(!__is_trivially_relocatable expr && !__builtin_is_cpp_trivially_relocatable expr)

BOTH((int));
BOTH((void *));
BOTH((Trivial));
BOTH((const Trivial));
BOTH((Trivial[3]));
BOTH((Derived));
BOTH((Bits));
NEITHER((int &));
NEITHER((void));
NEITHER((UserDtr));
NEITHER((UserCopy));
NEITHER((HasUserDtr));
NEITHER((VirtualBase));
// clang's own rule lets a const member be relocated, P2786's does not
static_assert(__is_trivially_relocatable(WithConst));
static_assert(!__builtin_is_cpp_trivially_relocatable(WithConst));
static_assert(__is_trivially_relocatable(WithRef));
static_assert(!__builtin_is_cpp_trivially_relocatable(WithRef));

static_assert(__is_trivially_equality_comparable(int));
static_assert(__is_trivially_equality_comparable(void *));
static_assert(__is_trivially_equality_comparable(Byte));
static_assert(!__is_trivially_equality_comparable(float));
static_assert(!__is_trivially_equality_comparable(int[3]));
static_assert(!__is_trivially_equality_comparable(void));

int main() { return 0; }
