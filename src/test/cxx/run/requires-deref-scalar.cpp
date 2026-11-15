// std: c++20
// requires(T &t) { *t; } is false for an integer (*i is ill-formed), so libc++'s iterator concepts reject int and
// vector::insert(pos, 5, 0xff) is the count overload, not the iterator-range template
template <class T> concept deref = requires(T &t) { *t; };
template <class T> concept negate = requires(T &t) { -t; };
static_assert(!deref<int> && deref<int *> && negate<int> && !negate<int *>);
template <class I> int pick(I) { if constexpr (deref<I>) return 1; else return 2; }
int main() { int a[2] = {}; return pick(a) == 1 && pick(5) == 2 ? 0 : 1; }
