// [over.ics.rank]/3.1.1: a list-initialization sequence that converts to
// std::initializer_list<X> is better than one that does not (f({S()}) with
// a deleted f(const S &) is f(initializer_list<S>)); [dcl.init.list]/3.7,
// [over.match.list]/1.1: {} of a class without a default constructor
// takes its initializer-list constructor (the array element left out of
// S s[3] = {{1, 2, 3}}); [class.mem]: a constructor template is a
// user-declared constructor, so the class is no POD for layout (dsize 0 of
// the empty class, as the Itanium C++ ABI's POD rule would make it 1).
namespace std {
template <class T> class initializer_list {
  const T *b;
  decltype(sizeof 0) n;
public:
  initializer_list(const T *, decltype(sizeof 0));
};
}
struct S {};
void f(const S &) = delete;
void f(std::initializer_list<S>);
void g(S s) { f({S()}); }
struct L {
  L(std::initializer_list<int>);
};
L array_of_three[3] = {{1, 2, 3}, {4, 5}};
L empty_list{};
struct T {
  template <class U> T(std::initializer_list<int>, U);
};
struct A {
  A(std::initializer_list<int>);
};
struct B {
  int i;
  A a;
};
struct C {
  std::initializer_list<int> a;
  B b;
  std::initializer_list<double> c;
};
C c = {};
struct DeletedTemplate {
  template <class U> DeletedTemplate(U) = delete;
};
