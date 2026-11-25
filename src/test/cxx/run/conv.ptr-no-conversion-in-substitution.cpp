/* [conv.ptr]: there is no implicit conversion from int* to a class
   pointer, nor from an enumeration's pointer to int*; in a substitution it
   is a deduction failure ([temp.deduct]/8).  libc++'s
   __has_destroy_v<allocator<__tree_node<...>>, int*> asks whether
   allocator<node>::destroy(node*) takes an int* (std::set::erase,
   Stockfish). */
template <class T> T &&declval();
struct node {
    int v;
};
struct allocator {
    void destroy(node *) {}
};
enum colour { red };
template <class A, class P, class = void> constexpr bool has_destroy = false;
template <class A, class P>
constexpr bool has_destroy<A, P, decltype((void)declval<A>().destroy(declval<P>()))> = true;
static_assert(has_destroy<allocator, node *>, "a node pointer");
static_assert(!has_destroy<allocator, int *>, "no int* to node*");
void take_int(int *) {}
template <class P, class = void> constexpr bool takes_int = false;
template <class P>
constexpr bool takes_int<P, decltype(take_int(declval<P>()))> = true;
static_assert(takes_int<int *>, "int*");
static_assert(!takes_int<colour *>, "no enumeration pointer to int*");
static_assert(!takes_int<node *>, "no class pointer to int*");
int destroyed = 0;
template <class A, class P> void destroy(A &a, P p)
{
    if constexpr (has_destroy<A, P>)
        a.destroy(p);
    else
        destroyed++;
}
int main()
{
    allocator a;
    node n{1};
    int i = 0;
    destroy(a, &n);
    destroy(a, &i);
    if (destroyed != 1)
        return 1;
    return 0;
}
