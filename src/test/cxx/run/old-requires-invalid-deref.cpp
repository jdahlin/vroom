// *i of an int is ill-formed for sure: a requirement on it is false, not unknown (libc++'s iterator_traits<int>
// chooses its primary template, so vector::insert(pos, 5, 0xff) is not the iterator-pair overload)
// std: c++20
template <class I> concept derefable = requires(I i) { *i; };
template <class I> struct traits { static constexpr int kind = 0; };
template <derefable I> struct traits<I> { static constexpr int kind = 1; };
int main() { return sizeof(char[derefable<int> ? 2 : 1]) == 1 && traits<int>::kind == 0 && traits<int *>::kind == 1 ? 0 : 1; }
