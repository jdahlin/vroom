// Type traits vcc evaluates: each static_assert is true in vcc (VCC_CXX_STATIC_ASSERT=1), clang accepts.
struct P { int a; char b; };
struct E { };
enum C { c0 };
static_assert(__is_trivially_copyable(int) && __is_trivially_copyable(char *) && __is_trivially_copyable(int[4]), "");
static_assert(__is_trivially_copyable(P) && __is_standard_layout(P) && __is_trivial(P) && __is_pod(P), "");
static_assert(__is_trivially_copyable(C) && __is_trivially_destructible(C), "");
static_assert(__is_empty(E) && !__is_empty(P) && !__is_empty(int), "");
static_assert(!__is_polymorphic(int) && !__is_abstract(C), "");
static_assert(__is_same(int, int) && !__is_same(int, const int), "");
static_assert(__is_pointer(int *) && !__is_pointer(int &) && __is_lvalue_reference(int &) && __is_rvalue_reference(int &&), "");
static_assert(__is_const(const int) && !__is_const(const int *) && __is_enum(C) && __is_class(P) && !__is_class(C), "");
static_assert(__is_signed(int) && !__is_signed(unsigned) && __is_unsigned(unsigned long) && __is_floating_point(double), "");
static_assert(__is_array(int[2]) && !__is_array(int *) && __is_function(void(int)) && __is_void(void), "");
