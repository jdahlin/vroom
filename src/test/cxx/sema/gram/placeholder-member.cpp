// [dcl.spec.auto.general]/5: a placeholder type may not be the type of a
// non-static data member (clang: "'auto' not allowed in non-static struct
// member"); bench/cxx-sema.py compares each verdict with clang's.

namespace plain {
struct A {
  auto m;
};
}

namespace initialized {
struct A {
  auto m = 1;
};
}

namespace pointer {
struct A {
  auto *m;
};
}

namespace bit_field {
struct A {
  auto m : 3;
};
}
