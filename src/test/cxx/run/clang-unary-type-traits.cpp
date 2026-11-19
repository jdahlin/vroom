/* [ext:clang_type_traits]: the traits of one type libc++'s <type_traits>
   uses when __has_builtin says they exist ([meta.unary.cat],
   [meta.unary.comp], [meta.unary.prop], [meta.unary.prop.query]), in a
   template's instantiation too */
struct S { int m; void f(); };
union U { int a; };
enum E { e };
enum class F { f };
template <class T> struct check { static const bool value = __is_class(T); };
static_assert(__is_void(void) && !__is_void(int), "");
static_assert(__is_integral(bool) && __is_integral(char) && !__is_integral(float), "");
static_assert(__is_floating_point(double) && !__is_floating_point(int), "");
static_assert(__is_arithmetic(long) && !__is_arithmetic(E), "");
static_assert(__is_array(int[2]) && __is_array(int[]) && !__is_array(int *), "");
static_assert(__is_bounded_array(int[2]) && !__is_bounded_array(int[]), "");
static_assert(__is_unbounded_array(int[]) && !__is_unbounded_array(int[1]), "");
static_assert(__is_pointer(int *) && !__is_pointer(int S::*), "");
static_assert(__is_lvalue_reference(int &) && !__is_lvalue_reference(int &&), "");
static_assert(__is_rvalue_reference(int &&) && __is_reference(int &), "");
static_assert(__is_function(int(int)) && !__is_function(int (*)(int)), "");
static_assert(__is_enum(E) && __is_enum(F) && !__is_enum(int), "");
static_assert(__is_scoped_enum(F) && !__is_scoped_enum(E), "");
static_assert(__is_class(S) && !__is_class(U) && __is_union(U), "");
static_assert(__is_member_pointer(int S::*), "");
static_assert(__is_member_function_pointer(void (S::*)()), "");
static_assert(__is_member_object_pointer(int S::*), "");
static_assert(!__is_member_object_pointer(void (S::*)()), "");
static_assert(__is_fundamental(decltype(nullptr)) && !__is_fundamental(S), "");
static_assert(__is_scalar(E) && __is_scalar(int *) && !__is_scalar(S), "");
static_assert(__is_compound(S) && !__is_compound(void), "");
static_assert(__is_object(S) && !__is_object(int &) && !__is_object(void), "");
static_assert(__is_const(const int) && !__is_const(const int *), "");
static_assert(__is_volatile(volatile int) && !__is_volatile(int), "");
static_assert(__is_signed(int) && __is_signed(float) && !__is_signed(char), "");
static_assert(__is_unsigned(bool) && __is_unsigned(char) && !__is_unsigned(E), "");
static_assert(__array_rank(int[2][3]) == 2 && __array_rank(int) == 0, "");
int main() {
  if (!check<S>::value || check<U>::value)
    return 1;
  return __is_integral(int) ? 0 : 2;
}
