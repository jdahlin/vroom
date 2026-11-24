/* [temp.mem]/1, [temp.mem.func]/1: a member of a member class template
   defined outside both, with the template-head of each: the body runs for
   a specialization of the outer and of the member class. */
template <class T> struct X {
    template <class U> struct Y {
        int f();
        int g(U u);
    };
};
template <class T> template <class U> int X<T>::Y<U>::f() {
    return sizeof(T) * 10 + sizeof(U);
}
template <class T> template <class U> int X<T>::Y<U>::g(U u) {
    return static_cast<int>(u) + static_cast<int>(sizeof(T));
}
int main() {
    X<char>::Y<int> a;
    X<short>::Y<char> b;
    if (a.f() != 14)
        return 1;
    if (b.f() != 21)
        return 2;
    if (b.g(5) != 7)
        return 3;
    return 0;
}
