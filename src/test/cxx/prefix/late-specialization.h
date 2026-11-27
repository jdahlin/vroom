/* a prefix whose templates are not instantiated before the stop: the main
   file may still specialize what they call ([temp.point]/7) */
template <int N> int which();
template <int N> struct list {
    int value;
    list() : value(which<N>()) {}
};
inline int use()
{
    list<1> l;
    return l.value;
}
