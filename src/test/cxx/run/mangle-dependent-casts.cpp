// [abi:expressions] casts in a value-dependent template argument or a decltype of a template's own
// declaration: cv <type> <e> for (T)e and T(e) (a simple-type-specifier keyword too), cv <type> _ E
// for T(), sc/rc/cc/dc <type> <e> for the named casts, sz <expression> for sizeof of one (clang's
// CodeGenCXX/mangle-exprs.cpp namespace Casts)
template <bool, typename = void> struct enable_if {
  typedef void type;
};
template <typename T> struct enable_if<false, T> {};
template <unsigned O> int implicit(typename enable_if<O <= 4>::type * = 0) { return 1; }
template <unsigned O> int cstyle(typename enable_if<O <= (unsigned)4>::type * = 0) { return 2; }
template <unsigned O> int functional(typename enable_if<O <= unsigned(4)>::type * = 0) { return 3; }
template <unsigned O> int static_(typename enable_if<O <= static_cast<unsigned>(4)>::type * = 0) { return 4; }
template <unsigned O, typename T>
int reinterpret_(typename enable_if<O <= sizeof(reinterpret_cast<T *>(0))>::type * = 0) {
  return 5;
}
template <typename T> int scalar_(decltype(T(), int())) { return 6; }
template <typename T> int long_(decltype(T() + long(1))) { return 7; }
int main() {
  return implicit<4>() + cstyle<4>() + functional<4>() + static_<4>() + reinterpret_<4, int>() + scalar_<int>(0) +
                     long_<int>(1) ==
                 28
             ? 0
             : 1;
}
