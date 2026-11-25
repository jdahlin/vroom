/* [abi:expressions] an unresolved name has the template arguments
   written, not the defaulted ones */
template <bool B, class T = void> struct en {};
template <class T> struct en<true, T> { using type = T; };
template <bool B, class T = void> using en_t = typename en<B, T>::type;
template <class T, bool S = true, bool C = !S && true> struct pathable { static const bool value = S; };
struct P {
    template <class T> using If = en_t<pathable<T>::value, P &>;
    template <class T> If<T> append(const T &) { return *this; }
};
int main()
{
    P p;
    p.append(1);
    return 0;
}
