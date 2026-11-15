// a = b of one class with no operator= declared: the implicit copy/move assignment, typed T& (an lvalue),
// so requires-expressions and concepts over it (libc++'s assignable_from, movable) are decided
// std: c++20
struct P { int x; };
template <class T> concept assignable = requires(T &l, T r) { l = r; };
int main() { return sizeof(char[assignable<P> ? 1 : 2]) == 1 && sizeof(char[requires(P &l, P &&r) { l = static_cast<P &&>(r); } ? 1 : 2]) == 1 ? 0 : 1; }
