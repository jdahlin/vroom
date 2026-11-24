// [temp.mem]/1: a member class template defined outside its class does not hide the class template's name
template <class T> struct S {
    template <class U> struct X;
    void h();
    template <class U> void g();
};
template <class T> template <class U> struct S<T>::X { U u; };
template <class T> void S<T>::h() {}
template <class T> template <class U> void S<T>::g() {}
S<int>::X<char> x;
