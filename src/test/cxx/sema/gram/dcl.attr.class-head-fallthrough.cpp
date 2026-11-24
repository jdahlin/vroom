// [dcl.attr.fallthrough]/1: fallthrough may be applied to a statement only;
// after the class-key it appertains to the class (clang: "cannot be applied
// to a declaration").
class [[fallthrough]] C {}; // expected-error {{'fallthrough' attribute cannot be applied to a declaration}}
struct [[likely]] D {}; // expected-error {{'likely' attribute cannot be applied to a declaration}}
