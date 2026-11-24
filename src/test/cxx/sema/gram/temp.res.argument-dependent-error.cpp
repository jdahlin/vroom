// [temp.res]/6, [temp.explicit]/10: an error in an instantiated body of a
// construct that names a template argument (t.foo() with T int, a return
// checked against the return type T) is the instantiation's: the explicit
// instantiation that asked for it is ill-formed, the other explicit
// instantiations are not
template <class T> void f(T t) { t.foo(); }
struct S { void foo(); };
template void f<S>(S);
template void f<int>(int); // expected-error@6 {{member reference base type}} expected-note {{in instantiation}}
template <class T> T g() { return 1; }
template int g<int>();
template void g<void>(); // expected-error@10 {{should not return a value}} expected-note {{in instantiation}}
