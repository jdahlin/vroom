// [abi:mangle.template-arg] a function with C language linkage, or main, as a template argument:
// L _Z <its name> E with no function type and no namespace (clang's CodeGenCXX/mangle-template.cpp
// test3-test5); a static local of such a function: Z <its name> E (no function type)
namespace ns {
extern "C" int c_f0(float) { return 3; }
} // namespace ns
template <int (&)(float)> struct t1 {
  int n = 1;
};
template <int (*)(float)> struct t2 {
  int n = 2;
};
template <int (*)(int, char **)> struct t3 {
  int n = 4;
};
int f1(t1<ns::c_f0> a) { return a.n; }
int f2(t2<&ns::c_f0> a) { return a.n; }
int main(int, char **);
int f3(t3<main> a) { return a.n; }
extern "C" int counter() {
  static int k = 0;
  return ++k;
}
int main(int, char **) {
  static int once = 5;
  counter();
  return f1(t1<ns::c_f0>()) + f2(t2<&ns::c_f0>()) + f3(t3<main>()) == 7 && counter() == 2 && once == 5 ? 0 : 1;
}
