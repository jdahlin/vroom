// [class.static.data]/1: static is written in the class only; an
// out-of-class definition of a class template's static data member with it
// is ill-formed at the definition, instantiated or not
// (bench/cxx-sema.py compares each verdict with clang's).
template<typename T> struct C { static int y; };
template<typename T> static int C<T>::y = 0; // expected-error {{'static' can only be specified inside the class definition}}
template<typename T> struct D { static int z; };
template<typename T> int D<T>::z = 0;
