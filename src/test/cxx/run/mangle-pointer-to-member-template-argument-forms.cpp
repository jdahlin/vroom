/* cxx-run-flags: -std=c++17 */
// [abi:mangle.template-arg] pointers to member as template arguments, the member's encoding with
// its function type and qualifiers (const, &, noexcept dropped as clang does), a member of a
// nested class, a parameter type spelled through a typedef or const typedef, a null one, the same
// argument twice (a substitution), as a return type and in an explicit instantiation of a function
// template (whose own declaration spells the parameter X T_ E)
struct A {
  int d = 1;
  int f(int) { return 2; }
  int g() const { return 3; }
  int h() & { return 4; }
  int k() noexcept { return 5; }
  struct N {
    int n = 6;
    int m() { return 7; }
  };
};
template <int A::*> struct D {};
template <int (A::*)(int)> struct F {};
template <int (A::*)() const> struct G {};
template <int (A::*)() &> struct H {};
template <int (A::*)() noexcept> struct K {};
template <int A::N::*> struct DN {};
template <int (A::N::*)()> struct MN {};
typedef int A::*dptr;
template <dptr> struct TD {};
template <const dptr> struct CTD {};
int t1(D<&A::d>) { return 1; }
int t2(F<&A::f>) { return 2; }
int t3(G<&A::g>) { return 3; }
int t4(H<&A::h>) { return 4; }
int t5(K<&A::k>) { return 5; }
int t6(DN<&A::N::n>) { return 6; }
int t7(MN<&A::N::m>) { return 7; }
int t10(TD<&A::d>) { return 10; }
int t11(CTD<&A::d>) { return 11; }
int t12(D<nullptr>) { return 12; }
int t13(D<&A::d>, D<&A::d>) { return 13; }
D<&A::d> t15() { return {}; }
template <int A::*P> int t16(D<P>) { return 16; }
template int t16<&A::d>(D<&A::d>);
int main() {
  t15();
  return t1({}) + t2({}) + t3({}) + t4({}) + t5({}) + t6({}) + t7({}) + t10({}) + t11({}) + t12({}) + t13({}, {}) +
                     t16<&A::d>({}) ==
                 100
             ? 0
             : 1;
}
