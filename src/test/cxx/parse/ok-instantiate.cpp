// Class template instances (re-parsed with their arguments): defaults, a nested class, an explicit
// specialization, a value argument, an alias template.  bench/cxx-parse-tests.py has clang check the layouts.
namespace n {
template <class T> struct allocator { T* hint; };
template <class T, class A = allocator<T>> struct vec { T* b; T* e; A a; struct node { T v; vec* next; }; node* head; };
template <class T> using ptr = T*;
template <class A, class B> struct pair { A first; B second; };
template <> struct pair<char, char> { int special[3]; };
template <class T, int N> struct arr { T elems[N]; };
}
n::vec<int> v1;
n::vec<double> v2;
n::pair<char, long> p1;
n::pair<char, char> p2;
n::pair<n::vec<short>, n::ptr<char>> p3;
n::arr<int, 5> a5;
n::vec<int>::node nd;
