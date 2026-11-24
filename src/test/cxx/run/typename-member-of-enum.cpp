/* [temp.deduct]/8, [basic.lookup.qual]/1: typename T::member with T an
   enumeration names nothing; in a partial specialization's argument that is
   a substitution failure, so the primary template is used. */
template <class T, class = void> struct S {
    static const int v = 1;
};
template <class T> struct S<T, typename T::member> {
    static const int v = 2;
};
enum class E { a };
enum F { b };
struct C {};
struct D {
    typedef void member;
};
static_assert(S<E>::v == 1);
static_assert(S<F>::v == 1);
static_assert(S<C>::v == 1);
static_assert(S<D>::v == 2);

int main()
{
    int sum = S<E>::v + S<F>::v + S<C>::v + S<D>::v;
    if (sum != 5)
        return 1;
    return 0;
}
