// Partial specializations: deduction through pointers, const, template-ids and values; partial
// ordering (const T* over T*); an explicit specialization over a partial one.  Layouts checked by clang.
namespace n {
template <class T> struct box { T v; };
template <class A, class B> struct pr { A a; B b; };
template <class T> struct s { char c; };                 // primary
template <class T> struct s<T*> { T* p; double d; };     // pointers
template <class T> struct s<const T*> { int x[3]; };     // more specialized than s<T*>
template <class T> struct s<box<T>> { T inner[2]; };     // a template pattern
template <class A, class B> struct s<pr<A, B>> { B b; A a; long z; };
template <> struct s<int*> { short only; };              // explicit beats partial
template <class T, int N> struct arr { T a[N]; };
template <class T> struct arr<T, 0> { char empty; };     // a value in the pattern
template <bool B, class T> struct cond { T t; };
template <class T> struct cond<false, T> { };            // value pattern, empty
}
n::s<long> a1; n::s<double*> a2; n::s<const char*> a3; n::s<n::box<short>> a4;
n::s<n::pr<char, double>> a5; n::s<int*> a6; n::arr<int, 3> a7; n::arr<int, 0> a8;
n::cond<true, int> a9; n::cond<false, int> a10;
