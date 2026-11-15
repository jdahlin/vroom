// std: c++20
// x = y of a class that declares no operator= has the type T& (its implicit one), so requires-expressions and
// libc++'s assignable_from / movable / weakly_incrementable hold for it (ranges::begin, std::inserter); a class
// template's instance is completed for __is_nothrow_destructible
struct W { int *p; };
template <class T> struct It { T *p; };
template <class T> concept assign_rv = requires(T &l, T &&r) { l = static_cast<T &&>(r); };
template <class A, class B> concept same = __is_same(A, B);
template <class T> concept assign_ref = requires(T &l, T &&r) { { l = static_cast<T &&>(r) } -> same<T &>; };
static_assert(assign_rv<W> && assign_ref<W> && assign_ref<It<int>> && __is_nothrow_destructible(It<long>));
int main() { W a{nullptr}, b{nullptr}; int x; b.p = &x; a = b; return a.p == &x ? 0 : 1; }
