/* [temp.param]/4: a class template's type-constraints, C<T, A...> naming
   an earlier parameter, and a non-type parameter of scoped enumeration
   type in its requires-clause */
template <class T> concept integral_like = sizeof(T) <= 8;
template <class T, class U> concept same_size = sizeof(T) == sizeof(U);
enum class kind : bool { unsized, sized };

template <integral_like I, same_size<I> S, kind K>
    requires(K == kind::sized || sizeof(S) == 4)
class range;

template <integral_like I, same_size<I> S, kind K>
    requires(K == kind::sized || sizeof(S) == 4)
class range {
public:
    I first;
    S last;
    int size() const { return K == kind::sized ? 2 : 1; }
};

int main()
{
    range<int, unsigned, kind::sized> a{1, 2};
    range<long, long, kind::sized> b{3, 4};
    range<int, int, kind::unsized> c{5, 6};
    return a.size() + b.size() + c.size() == 5 && a.first + b.last + c.first == 10
               ? 0
               : 1;
}
