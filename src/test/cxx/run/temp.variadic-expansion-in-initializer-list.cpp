/* [temp.variadic]/7, [dcl.init.list]/5: a pack expansion in a braced list
   that initializes std::initializer_list<E>: its backing array const E[N]
   has an element per element of the pack; an element a variable
   template's specialization ([expr.prim.id]/1: a simple-template-id).
   libc++'s <variant>: __common_trait({__trait<_Types, ...>...}) (std::variant
   in Stockfish's score.h). */
typedef decltype(sizeof 0) size_type;
namespace std
{
template <class T> class initializer_list
{
    const T *p;
    size_type n;

public:
    constexpr const T *begin() const { return p; }
    constexpr const T *end() const { return p + n; }
    constexpr size_type size() const { return n; }
};
} // namespace std
enum trait { trivial, available, unavailable };
template <class T> constexpr trait trait_of = sizeof(T) > 2 ? available : trivial;
constexpr trait common(std::initializer_list<trait> traits)
{
    trait result = trivial;
    for (trait t : traits)
        if (t > result)
            result = t;
    return result;
}
template <class... T> struct traits {
    static constexpr trait copy = common({trait_of<T>...});
    static size_type count() { return std::initializer_list<int>{sizeof(T)...}.size(); }
};
static_assert(traits<char, short>::copy == trivial, "all trivial");
static_assert(traits<char, int>::copy == available, "one available");
int sum(std::initializer_list<int> l)
{
    int s = 0;
    for (int v : l)
        s += v;
    return s;
}
template <class... T> int sizes() { return sum({static_cast<int>(sizeof(T))...}); }
int main()
{
    if (sizes<char, short, int>() != 7)
        return 1;
    if (traits<char, short, int>::count() != 3)
        return 2;
    trait t = traits<int, char>::copy;
    if (t != available)
        return 3;
    return 0;
}
