/* [temp.param]/4, [temp.arg.nontype]/2: a partial specialization whose
   argument names a non-type parameter typed by an earlier type parameter
   (libc++'s __deque_iterator<T, P, R, M, D, D BlockSize>). */
template <class T, class D, D B> struct It {};
template <class I> struct Tr { static const int v = 1; };
template <class T, class D, D B>
struct Tr<It<T, D, B> > { static const int v = 2; };
template <class T, class D, D B> struct Block { static const D v = B; };
template <class T, class D, D B>
struct Tr<Block<T, D, B> > { static const int v = static_cast<int>(B) + 10; };
static_assert(Tr<It<int, long, 3> >::v == 2);
static_assert(Tr<int>::v == 1);
int main()
{
    return Tr<Block<int, long, 5> >::v == 15 ? 0 : 1;
}
