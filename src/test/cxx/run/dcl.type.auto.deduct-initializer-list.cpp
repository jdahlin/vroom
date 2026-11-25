/* [dcl.type.auto.deduct]: auto x = { ... } is a std::initializer_list */
typedef decltype(sizeof 0) size_type;
namespace std
{
template <class T> class initializer_list
{
    const T *p;
    size_type n;

public:
    constexpr const T *begin() const
    {
        return p;
    }
    constexpr const T *end() const
    {
        return p + n;
    }
    constexpr size_type size() const
    {
        return n;
    }
};
} // namespace std
int main()
{
    const auto factors = {12u, 8u, 4u, 0u};
    unsigned sum = 0;
    for (const auto factor : factors)
        sum = sum * 100 + factor;
    int k = 3;
    auto more = {k, k + 1};
    return !(sum == 12080400u && factors.size() == 4 && *more.begin() == 3 &&
             more.size() == 2);
}
