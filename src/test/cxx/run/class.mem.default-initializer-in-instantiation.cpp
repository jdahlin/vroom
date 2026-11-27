/* [temp.res]/10 against [class.mem]/7: a default member initializer first
   needed inside an instantiation looks its names up from its class, not
   from the template (Skia's SpecializedCallKey in std::pair) */
template <class T> constexpr int first()
{
    T t{};
    return t.i;
}
template <class T> struct Holder {
    static int make()
    {
        T t{};
        return t.i + t.j;
    }
};
static constexpr int kLate = -1;
namespace N
{
static int late = 40;
}
struct K {
    int i = kLate;
    int j = N::late;
};
class P
{
    static constexpr int kPrivate = 7;

public:
    int i = kPrivate;
    int j = kLate;
};
struct Q {
    int i = kLate;
    int j = 2;
};
static_assert(first<Q>() == -1);

int main()
{
    if (Holder<K>::make() != 39)
        return 1;
    if (Holder<P>::make() != 6)
        return 2;
    if (Holder<Q>::make() != 1)
        return 3;
    return 0;
}
