// [dcl.spec.auto.general]/4: a placeholder in a variable template with an
// initializer; the type is deduced in each specialization.
template <class T> auto vp_v = T(1);
template <auto x> constexpr auto vp_c = x;
template <class> auto vp_fn = [] {};
int vp_a = vp_v<int>;
long vp_b = vp_v<long>;
int vp_d = vp_c<3>;
void vp_f() { vp_fn<int>(); }
