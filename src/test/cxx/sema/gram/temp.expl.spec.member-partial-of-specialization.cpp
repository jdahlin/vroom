// [temp.expl.spec]/16: a partial specialization of a member class template of an
// implicit instantiation, defined for it alone.
template<class T> struct X0 { template<class U> struct I { static const unsigned value = 0; }; };
template<> template<class Z> struct X0<float>::I<Z*> { static const unsigned value = 3; };
static_assert(X0<float>::I<int*>::value == 3, "");
