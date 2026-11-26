// [stmt.ranged]/1, [dcl.type.auto.deduct]/4: a braced-init-list as the range,
// of one element and of several
namespace std
{ // as libc++'s: the compiler fills in the pointer and the length
template <class E> class initializer_list
{
    const E *begin_;
    decltype(sizeof 0) size_;

  public:
    constexpr initializer_list() noexcept : begin_(nullptr), size_(0) {}
    constexpr const E *begin() const noexcept
    {
        return begin_;
    }
    constexpr const E *end() const noexcept
    {
        return begin_ + size_;
    }
};
} // namespace std

int main()
{
    int sum = 0;
    for (auto byte : {
             // one element
             0xc1})
        sum += byte;
    int k = 2;
    for (auto v : {k})
        sum += v;
    for (int v : {1, k, 3})
        sum += v;
    return sum - (0xc1 + 2 + 6);
}
