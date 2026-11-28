/* [dcl.type.auto.deduct]/4, [dcl.init.list]/5-6: copy-list-initialized auto
   variables of static storage duration deduce std::initializer_list, whose
   backing array lives as long as they do */
namespace std
{
template <class E> class initializer_list
{
    const E *b;
    unsigned long n;

  public:
    constexpr initializer_list() : b(nullptr), n(0) {}
    constexpr const E *begin() const { return b; }
    constexpr const E *end() const { return b + n; }
    constexpr unsigned long size() const { return n; }
};
} // namespace std
static int two = 2;
static int get() { return two; }
static const auto outer = {1, get(), 3};
static int sum(std::initializer_list<int> l)
{
    int s = 0;
    for (int x : l)
        s += x;
    return s;
}
static int lines(int more)
{
    static const auto words = {more, 2};
    static auto first = {get(), more};
    return sum(words) + sum(first);
}
int main()
{
    if (sum(outer) != 6)
        return 1;
    if (lines(5) != 14 || lines(100) != 14)
        return 2;
    return 0;
}
