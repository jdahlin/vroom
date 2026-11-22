// [ext:clang_type_traits]: __is_pod, __is_literal, __has_trivial_constructor,
// __has_trivial_copy, __has_trivial_assign, __has_unique_object_representations
// and __is_standard_layout of classes with bases, answered as clang does.
struct Empty {};
struct Plain { int a; char b; };
struct Packed2 { int a; int b; };
struct Padded { char c; int i; };
struct Derived : Plain {};
struct TwoBases : Empty, Packed2 {};
struct SplitData : Plain { int more; };
struct Virt { virtual void f(); };
struct UserCtor { UserCtor(int); };
struct WithAssign { void operator=(int); };
union U { int i; float f; };
union UEq { int a; unsigned b; };

static_assert(__is_pod(Plain) && __is_pod(int) && __is_pod(Derived));
static_assert(!__is_pod(Virt) && !__is_pod(UserCtor) && !__is_pod(void));
static_assert(__is_pod(WithAssign));
static_assert(__is_standard_layout(Derived) && __is_standard_layout(TwoBases));
static_assert(!__is_standard_layout(SplitData) && !__is_standard_layout(Virt));
static_assert(__is_literal(int) && __is_literal(Plain) && __is_literal_type(Empty));
static_assert(__has_trivial_constructor(Plain) && !__has_trivial_constructor(UserCtor));
static_assert(__has_trivial_copy(Plain) && __has_trivial_assign(Plain));
static_assert(!__has_trivial_assign(const int));
static_assert(__has_unique_object_representations(Packed2));
static_assert(__has_unique_object_representations(int[4]));
static_assert(!__has_unique_object_representations(Padded));
static_assert(!__has_unique_object_representations(Empty));
static_assert(!__has_unique_object_representations(float));
static_assert(!__has_unique_object_representations(U));
static_assert(__has_unique_object_representations(UEq));
static_assert(__has_unique_object_representations(TwoBases));

int main() { return 0; }
