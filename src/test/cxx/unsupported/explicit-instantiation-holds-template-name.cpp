/* An explicit instantiation the analysis refuses (here a pointer to member of a template
   parameter's class as a template-parameter, [temp.arg.nontype]) holds only the names it
   names outside template-argument-lists ([temp.explicit]/1: the template's): the class A of
   its template arguments stays known, and the declarations after it that use A are analysed
   (and the symbols of those before, t1's D<&A::d>, mangled). */
struct A {
  int d = 1;
};
template <int A::*> struct D {};
int t1(D<&A::d>) { return 1; }
template <class T, int T::*P> int t18(D<P>) { return 18; }
template int t18<A, &A::d>(D<&A::d>);
int t2(A a) { return a.d; }
