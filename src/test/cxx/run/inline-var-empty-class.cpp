// inline constexpr E e = E(); of an empty class (libc++'s piecewise_construct): defined (weak), not left
// a declaration whose address is null
namespace n { struct E { explicit E() = default; }; inline constexpr E e = E(); }
template <class T> const n::E *g(T) { return &n::e; }
int main() { return g(1) == &n::e && &n::e != nullptr ? 0 : 1; }
