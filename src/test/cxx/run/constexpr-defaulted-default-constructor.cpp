/* [dcl.fct.def.default]/3: an explicitly-defaulted default constructor
   declared constexpr (libc++'s tuple<>, `constexpr tuple() noexcept =
   default;`): constexpr-compatible without virtual bases, also with a member
   left uninitialized (C++20).  The class is complete and usable; its
   explicit specialization named earlier in a template is defined. */
template <class...> class T;
template <class... Ts> struct use { using type = T<Ts...>; };
template <> class T<> {
public:
  constexpr T() noexcept = default;
  int f() const { return 3; }
};
struct M {
  constexpr M() = default;
  int i;
};
T<> make() { return T<>(); }
int main()
{
  constexpr T<> t;
  M m;
  m.i = 4;
  return make().f() + t.f() + m.i - 10;
}
