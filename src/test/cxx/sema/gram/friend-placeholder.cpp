// [dcl.spec.auto]/1 in a friend declaration: a friend function's return
// type can be a placeholder (a declaration, a definition in the class, a
// definition in a class template); bench/cxx-sema.py compares each verdict
// with clang's.

namespace decl {
struct A {
  friend auto f(A);
};
}

namespace def {
struct A {
  friend auto f(A) { return 1; }
};
}

namespace in_template {
template <class T> struct W {
  friend auto g(W) { return 1; }
};
using S = W<int>;
}
