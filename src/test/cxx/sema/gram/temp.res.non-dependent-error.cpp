// [temp.res]/6, /8: an error in a template's body of a construct that names
// no template argument is the template's, which clang reports at its
// definition: an explicit instantiation of it adds nothing and is
// well-formed (-pt of a T* is checked by the pointer's form alone)
template <class T> void g(T) { int *p = 1.5; } // expected-error {{cannot initialize}}
template void g<int>(int);
template <class T> void h(T *pt) { -pt; } // expected-error {{invalid argument type}}
template void h<int>(int *);
template <class T> void k() { auto a = 0, b = 0.0, c = T(); } // expected-error {{deduced as 'int'}}
template void k<int>();
template void k<double>();
