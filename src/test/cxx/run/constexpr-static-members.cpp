/* [dcl.constexpr]/10, [dcl.inline]/1: a constexpr static data member is
   const and an inline variable defined in its class, usable in constant
   expressions; an odr-use emits it (COMDAT, as clang).  [dcl.typedef]: a
   typedef or alias-declaration in a class is a member type.  [temp.param]/4:
   a non-type parameter of an earlier type parameter's type.  libc++'s
   integral_constant: template <class T, T v> struct integral_constant. */
template <class T, T v> struct integral_constant {
    static inline constexpr const T value = v; /* libc++'s spelling */
    typedef T value_type;
    using type = integral_constant;
};
typedef integral_constant<bool, true> true_type;
typedef integral_constant<long, 1L << 40> big;
struct Limits {
    static constexpr int max = 100;
    static constexpr unsigned char small = 7;
    static inline const int nine = 9;
    typedef short half;
};
static_assert(true_type::value && big::value == 1L << 40 && Limits::max == 100);
static const int *address() { return &Limits::max; }   /* an odr-use */
static const int *nine() { return &Limits::nine; }
int main()
{
    true_type::value_type b = true_type::value;
    big::type::value_type l = big::value;
    Limits::half h = 3;
    int arr[Limits::small];
    return !b + (l >> 40 != 1) * 2 + (*address() != 100) * 4 + (h != 3) * 8 +
           (sizeof arr != 7 * sizeof(int)) * 16 + (*nine() != 9) * 32;
}
