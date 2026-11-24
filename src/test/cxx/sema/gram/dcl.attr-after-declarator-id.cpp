// [dcl.attr.grammar]/5, [dcl.attr.noreturn]/1, [dcl.attr.fallthrough]/1: an
// attribute-specifier-seq after the declarator-id appertains to the entity
// declared; noreturn to a function only, fallthrough to a statement only
void a2 [[noreturn]] () { for (;;) {} }
template <typename T> void a3 [[noreturn]] () { for (;;) {} }
void a4 [[deprecated]] ();
int a5 [[maybe_unused]];
int d2 [[noreturn]]; // expected-error {{'noreturn' attribute only applies to functions}}
typedef int n [[fallthrough]]; // expected-error {{'fallthrough' attribute cannot be applied to a declaration}}
