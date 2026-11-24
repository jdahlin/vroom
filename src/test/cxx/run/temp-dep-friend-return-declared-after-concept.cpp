/* [temp.res]/10: the return type of a friend function template names a class
   declared after the concept whose requirement calls the friend; the name is
   looked up at the friend's declaration, not at the concept's. */
namespace N {
template <class T, class U>
concept Less = requires(T a, U b) { a < b; };
template <class T, class U>
concept Both = Less<T, U> && Less<U, T>;

struct Order {
    int v;
};

template <class T>
struct Wrap {
    T i;
    template <class U>
    friend constexpr Order operator<(const Wrap &x, const Wrap<U> &y) noexcept
    {
        Order o = {x.i < y.i};
        return o;
    }
};
} // namespace N

static_assert(N::Both<N::Wrap<int>, N::Wrap<int>>);

int main()
{
    N::Wrap<int> a = {1}, b = {2};
    if (!(a < b).v)
        return 1;
    if ((b < a).v)
        return 2;
    return 0;
}
