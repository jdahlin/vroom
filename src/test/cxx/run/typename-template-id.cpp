/* [temp.res]/3: typename nested-name-specifier template_opt
   simple-template-id, the template a member of the namespace named (an
   alias template, a class template); an alias template whose type is
   analysed with each use's arguments */
namespace pick {
template <class R, class E> using first = R;
template <class R, class E> struct second {
    typedef E type;
};
} // namespace pick
template <class R, class E> using first_t = typename pick::template first<R, E>;
template <class R, class E> using second_t = typename pick::second<R, E>;
typedef typename pick::second<char, long>::type plain;

static_assert(sizeof(first_t<char, int>) == 1, "");
typedef second_t<char, int>::type picked;
static_assert(sizeof(picked) == 4, "");
static_assert(sizeof(plain) == 8, "");

int main()
{
    first_t<int, char> a = 3;
    picked b = 4;
    return a + b - 7;
}
