// __has_unique_object_representations ([meta.unary.prop], [ext:clang_type_traits]) of void, functions, references, std::nullptr_t and
// _BitInt(N) (unique when N fills its bytes); every value is clang++'s.
#define H(T) static_assert(__has_unique_object_representations(T), #T)
#define N(T) static_assert(!__has_unique_object_representations(T), #T)
N(void);
N(const void);
N(volatile void);
N(int(int));
N(int(int) const);
N(int &);
N(const int &);
N(int &&);
N(decltype(nullptr));
H(_BitInt(8));
H(_BitInt(16));
H(_BitInt(32));
H(_BitInt(64));
H(_BitInt(128));
N(_BitInt(7));
N(_BitInt(24));
N(_BitInt(33));
N(_BitInt(65));
N(_BitInt(96));
H(_BitInt(8)[3]);
N(_BitInt(7)[3]);
H(unsigned _BitInt(16));
struct WithRef { int &r; };
struct RefTail { int &r; char a; };
H(WithRef);
N(RefTail);
