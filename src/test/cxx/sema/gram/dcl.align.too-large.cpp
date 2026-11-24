// [dcl.align]/2: an alignment the implementation does not support is
// ill-formed (clang: 4294967296 bytes or smaller on Linux), on a variable, a
// class and a data member after its declarator-id; [basic.align]/4 a power
// of two.
alignas(0x200000000) int n; // expected-error {{requested alignment must be 4294967296 bytes or smaller}}
struct alignas(0x200000000) X {}; // expected-error {{requested alignment must be 4294967296 bytes or smaller}}
struct Y {
  int n alignas(0x200000000); // expected-error {{requested alignment must be 4294967296 bytes or smaller}}
};
alignas(3) int bad; // expected-error {{requested alignment is not a power of 2}}
struct Z {
  int a alignas(1 << 28);
  char b alignas(2);
};
alignas(1 << 30) int big;
