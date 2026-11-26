// [temp.param]/4, [dcl.type.class.deduct]: class template placeholder as a
// non-type template parameter type (qualified and unqualified).
namespace detail {
template <int N>
struct fixed {
    int v = N;
};
} // namespace detail

template <int N>
struct plain {
    int v = N;
};

template <detail::fixed F>
constexpr int get_qualified()
{
    return F.v;
}

template <plain P>
constexpr int get_unqualified()
{
    return P.v;
}

int main()
{
    return 0;
}
