// [basic.namespace] Example 1 (paragraph 2), docs/cxx-spec/dcl/basic.namespace.md
// needs: P8
// status: well-formed (as far as the example says)

export module M;
namespace N1 {}                 // N1 is not exported
export namespace N2 {}          // N2 is exported
namespace N3 { export int n; }  // N3 is exported
