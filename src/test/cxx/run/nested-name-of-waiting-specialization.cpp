// [temp.inst]/2: a specialization named in the member-specification of its
// own argument (it<bj>, bj being defined) is instantiated where one of its
// members is named: typename iterator::difference_type.  The pointer
// difference of two pointers to the class being defined in a decltype is
// no completely-defined object type ([expr.add]/2), a substitution failure
// that leaves the other overload.
template <class B> class it {
public:
    using difference_type = long;
    B *p;
};
template <class P, class Q> auto minus(P a, Q b) -> decltype(a - b)
{
    return a - b;
}
template <class P, class Q> long minus(P, Q, ...);
template <class T = int> class bj {
public:
    using iterator = it<bj>;
    iterator set(iterator i, typename iterator::difference_type n)
    {
        i.p = 0;
        return i;
    }
    using R = decltype(minus(static_cast<bj *>(nullptr), static_cast<bj *>(nullptr)));
    T t;
};
int main()
{
    bj<> b;
    it<bj<>> i;
    i.p = &b;
    if (b.set(i, 3).p != 0)
        return 1;
    bj<>::R r = 5;
    if (r != 5)
        return 2;
    return 0;
}
