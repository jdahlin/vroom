/* [temp.friend]/1: friend struct hs<vec>; only names the specialization: no
   partial specialization is chosen (by the time of the use two match, neither
   more specialized) */
template <class T> struct al {
};
template <class T, class A = al<T>> struct vec {
};
template <class T> struct hs {
    static constexpr int which = 0;
};
template <class A> struct hs<vec<bool, A>> {
    static constexpr int which = 1;
};
template <class A> struct vec<bool, A> {
    friend struct hs<vec>;
    int n = 3;
};
template <class T> struct hs<vec<T>> {
    static constexpr int which = 2;
};
int main()
{
    vec<bool> v;
    return v.n != 3 || hs<vec<int>>::which != 2;
}
