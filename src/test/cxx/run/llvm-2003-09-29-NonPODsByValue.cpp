// llvm-test-suite SingleSource/Regression/C++/2003-09-29-NonPODsByValue.cpp
// (Apache-2.0 WITH LLVM-exception): a class with a destructor by value
#include <stdio.h>

struct C {
  unsigned bv;
  ~C() {}
};

static unsigned test(C b) {
  return b.bv;
}


int main() {
  C c;
  c.bv = 1234;
  printf("%d\n", test(c));
  return 0;
}
