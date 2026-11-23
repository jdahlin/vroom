// [dcl.array] Example 2 (paragraph 5), docs/cxx-spec/dcl/dcl.array.md
// needs: P5
// status: well-formed (as far as the example says)

typedef int A[5], AA[2][3];
typedef const A CA;             // type is ``array of 5 const int''
typedef const AA CAA;           // type is ``array of 2 array of 3 const int''
