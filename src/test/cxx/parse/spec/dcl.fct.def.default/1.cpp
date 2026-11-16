// [dcl.fct.def.default] Example 1 (paragraph 4), docs/cxx-spec/dcl/dcl.fct.def.default.md
// needs: P5
// status: annotated

struct S {
  constexpr S() = default;              // error: implicit S() is not constexpr // semantic
  S(int a = 0) = default;               // error: default argument // semantic
  void operator=(const S&) = default;   // error: non-matching return type // semantic
  ~S() noexcept(false) = default;       // OK, despite mismatched exception specification
private:
  int i;
  S(S&);                                // OK: private copy constructor
};
S::S(S&) = default;                     // OK: defines copy constructor

struct T {
  T();
  T(T &&) noexcept(false);
};
struct U {
  T t;
  U();
  U(U &&) noexcept = default;
};
U u1;
U u2 = static_cast<U&&>(u1);            // OK, calls std::terminate if T::T(T&&) throws
