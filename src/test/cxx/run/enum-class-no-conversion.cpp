// an enum class converts to no integer implicitly (a plain enum does): f(int) does not take E::b, so
// json's value_t is not printable through operator<<(ostream&, int) for doctest's has_insertion_operator
// std: c++20
enum class E : unsigned char { a, b };
enum P { x, y };
static int f(int v) { return v + 1; }
template <class T> concept takes_int = requires(T t) { f(t); };
int main() { return !takes_int<E> && takes_int<P> && f(y) == 2 && f(static_cast<int>(E::b)) == 2 ? 0 : 1; }
