/* [over.over]/1: an overloaded function's name as a non-type template
   argument is the function the parameter's type selects */
struct Block { int v; };
using IndexFn = int (*)(const Block *);
template <typename T, typename B> using ItemFn = T (*)(B *, int);
template <bool C, class A, class B> struct cond { using type = A; };
template <class A, class B> struct cond<false, A, B> { using type = B; };
template <typename T, bool Const, IndexFn Start,
          ItemFn<T, typename cond<Const, const Block, Block>::type> Resolve>
struct It {
    static int get(Block *b) { return Start(b) + Resolve(b, 1); }
};
template <typename T> struct List {
    static T &GetItem(Block *b, int) { return b->v; }
    static const T &GetItem(const Block *b, int) { return b->v; }
    static int First(const Block *b) { return b->v; }
    using Iter = It<T &, false, &First, &GetItem>;
    using CIter = It<const T &, true, &First, GetItem>;
};
int twice(int x) { return 2 * x; }
int twice(int x, int y) { return x + y; }
template <int (*F)(int)> int call1() { return F(21); }
template <int (&F)(int, int)> int call2() { return F(40, 2); }
int main()
{
    Block b{20};
    if (List<int>::Iter::get(&b) != 40 || List<int>::CIter::get(&b) != 40)
        return 1;
    if (call1<&twice>() != 42 || call1<twice>() != 42)
        return 2;
    if (call2<twice>() != 42)
        return 3;
    return 0;
}
