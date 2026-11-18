/* CWG 1980: two partial specializations whose arguments are the same once
   void_t folds them (void_t<typename T::a>, void_t<typename T::b>): clang
   reports a redefinition and then uses neither; the template is not used */
template <class...> using void_t = void;
template <class T, class = void> struct trait {
    static const int value = 0;
};
template <class T> struct trait<T, void_t<typename T::a>> {
    static const int value = 1;
};
template <class T> struct trait<T, void_t<typename T::b>> {
    static const int value = 2;
};
struct B {
    typedef int a;
};
int x = trait<B>::value;
