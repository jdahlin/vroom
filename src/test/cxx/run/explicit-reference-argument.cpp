// a parameter whose template argument is given explicitly takes part in no deduction: s3<Pol, Less &>(a, b, l)
// with C = Less & binds l (libc++'s std::__sort3<_AlgPolicy, _Compare> from std::sort)
struct Less { bool operator()(int a, int b) const { return a < b; } };
struct Pol {};
template <class P, class C, class It> int s3(It a, It b, C c) { return c(*a, *b) ? 1 : 2; }
template <class P, class C, class It> int intro(It a, It b, C c) { return s3<P, C>(a, b, c); }
int f1(int *a) { Less l; return intro<Pol, Less &>(a, a + 1, l); }
int f2(int *a) { Less l; return s3<Pol, Less &>(a, a + 1, l); }
int f3(int *a) { Less l; return s3<Pol, Less>(a, a + 1, l); }
int main() { int a[2] = {1, 2}; return f1(a) + f2(a) + f3(a) == 3 ? 0 : 1; }
