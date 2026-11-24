// [class.prop]/1 as clang answers __is_trivially_copyable: a deleted copy or
// move constructor or assignment operator is trivial (no cwg1734); a
// user-provided one, a user-provided destructor or a non-trivial subobject
// is not.
#define T(name, ...) struct name { name() = default; __VA_ARGS__ };
T(DelCopyDefMove, DelCopyDefMove(const DelCopyDefMove&) = delete; DelCopyDefMove(DelCopyDefMove&&) = default; int x;)
T(DefMove, DefMove(DefMove&&) = default; int x;)
T(DefMoveAssign, DefMoveAssign& operator=(DefMoveAssign&&) = default; int x;)
T(DelMove, DelMove(DelMove&&) = delete; int x;)
T(DelAssign, DelAssign& operator=(const DelAssign&) = delete; int x;)
T(DelAll, DelAll(const DelAll&) = delete; DelAll& operator=(const DelAll&) = delete; int x;)
T(UserDtor, ~UserDtor() {} int x;)
T(DelDtor, ~DelDtor() = delete; int x;)
T(UserCopy, UserCopy(const UserCopy&) {} int x;)
T(UserMove, UserMove(UserMove&&) {} int x;)
T(DefCopy, DefCopy(const DefCopy&) = default; int x;)
T(Plain, int x;)
struct NT { NT(const NT&) {} };
T(MemNT, NT n;)
struct Dc { Dc(const Dc&) = delete; };
T(MemDel, Dc d;)
T(MemDelDefMoveAssign, Dc d; MemDelDefMoveAssign& operator=(MemDelDefMoveAssign&&) = default;)
T(DelCopyDefAssign, DelCopyDefAssign(const DelCopyDefAssign&) = delete; DelCopyDefAssign& operator=(const DelCopyDefAssign&) = default; int x;)
static_assert(__is_trivially_copyable(DelCopyDefMove) == true, "DelCopyDefMove");
static_assert(__is_trivially_copyable(DefMove) == true, "DefMove");
static_assert(__is_trivially_copyable(DefMoveAssign) == true, "DefMoveAssign");
static_assert(__is_trivially_copyable(DelMove) == true, "DelMove");
static_assert(__is_trivially_copyable(DelAssign) == true, "DelAssign");
static_assert(__is_trivially_copyable(DelAll) == true, "DelAll");
static_assert(__is_trivially_copyable(UserDtor) == false, "UserDtor");
static_assert(__is_trivially_copyable(DelDtor) == true, "DelDtor");
static_assert(__is_trivially_copyable(UserCopy) == false, "UserCopy");
static_assert(__is_trivially_copyable(UserMove) == false, "UserMove");
static_assert(__is_trivially_copyable(DefCopy) == true, "DefCopy");
static_assert(__is_trivially_copyable(Plain) == true, "Plain");
static_assert(__is_trivially_copyable(MemNT) == false, "MemNT");
static_assert(__is_trivially_copyable(MemDel) == true, "MemDel");
static_assert(__is_trivially_copyable(MemDelDefMoveAssign) == true, "MemDelDefMoveAssign");
static_assert(__is_trivially_copyable(DelCopyDefAssign) == true, "DelCopyDefAssign");

struct NTd { ~NTd() {} };
struct NTa { NTa& operator=(const NTa&) { return *this; } };
struct DelA { DelA& operator=(const DelA&) = delete; };
struct V { virtual void f(); };
struct B1 { int a; };
struct D1 : B1 { int b; };
struct D2 : NTd { int b; };
struct M1 { NTd m; };
struct M2 { NTa m[2]; };
struct M3 { DelA m; };
struct M4 { V v; };
struct Mv { Mv(Mv&&) {} };
struct M5 { Mv m; };
struct MvDel { MvDel(MvDel&&) = delete; };
struct M6 { MvDel m; };
struct U { int a; float b; };
static_assert(__is_trivially_copyable(D1) == true);
static_assert(__is_trivially_copyable(D2) == false);
static_assert(__is_trivially_copyable(M1) == false);
static_assert(__is_trivially_copyable(M2) == false);
static_assert(__is_trivially_copyable(M3) == true);
static_assert(__is_trivially_copyable(M4) == false);
static_assert(__is_trivially_copyable(M5) == false);
static_assert(__is_trivially_copyable(M6) == true);
static_assert(__is_trivially_copyable(U) == true);

int main() { return 0; }
