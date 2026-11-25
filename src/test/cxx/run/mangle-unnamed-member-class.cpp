// [abi:mangling.named] an unnamed class that is a member of a class: Ut [ <number> ] _, counting
// the unnamed classes before it that have no typedef name for linkage ([dcl.typedef]/9: a member
// typedef names one too) (clang's CodeGenCXX/mangle.cpp test37, test38)
struct foo {
  struct {
    int k = 1;
  } a;
  typedef struct {
    int k = 2;
  } b;
  typedef struct {
    int k = 3;
  } *c;
  struct {
    int k = 4;
  } d;
};
template <typename T> int func(T t) { return t.k; }
typedef struct {
  struct {
    int k = 5;
  } a;
} bar;
int main() {
  foo f;
  foo::c p = nullptr;
  __typeof__(*p) e;
  return func(f.a) + func(foo::b()) + func(e) + func(f.d) + func(bar().a) == 15 ? 0 : 1;
}
