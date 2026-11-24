// [temp.expl.spec]/1: template<> class-key nested-name-specifier simple-template-id ; declares an explicit specialization of a class template in another namespace
namespace N {
template <class T> struct B { static const int v = 0; };
template <> struct B<char> { static const int v = 2; };
}
template <> struct N::B<int>;
template <> struct N::B<int> { static const int v = 1; };
static_assert(N::B<int>::v == 1, "");
static_assert(N::B<char>::v == 2, "");
static_assert(N::B<long>::v == 0, "");
