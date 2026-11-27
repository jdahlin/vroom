/* [abi:expressions] in dependent signatures */
template <int N> struct I {};
template <class T> struct has { static const int value = 1; };
template <int N> void arith(I<N + 1>) {}
template <int N> void unary(I<-N>) {}
template <class T> void sz(I<sizeof(T)>) {}
template <class T> void al(I<alignof(T)>) {}
template <class T> void member_value(I<has<T>::value>) {}
template <class T> auto call(T t) -> decltype(t.size()) { return t.size(); }
struct V {
    int size() { return 0; }
};
template <class... T> void count(I<sizeof...(T)>) {}
template <int N> void cond(I<(N > 1) ? 1 : 2>) {}
template <class T> auto cast(T t) -> decltype(static_cast<long>(t)) { return t; }
void use()
{
    arith<1>(I<2>());
    unary<1>(I<-1>());
    sz<int>(I<4>());
    al<int>(I<4>());
    member_value<int>(I<1>());
    call(V());
    count<int, char>(I<2>());
    cond<2>(I<1>());
    cast(1);
}
// MANGLE: _Z5arithILi1EEv1IIXplT_Li1EEE
// MANGLE: _Z5unaryILi1EEv1IIXngT_EE
// MANGLE: _Z2szIiEv1IIXstT_EE
// MANGLE: _Z2alIiEv1IIXatT_EE
// MANGLE: _Z12member_valueIiEv1IIXsr3hasIT_EE5valueEE
// MANGLE: _Z4callI1VEDTcldtfp_4sizeEET_
// MANGLE: _Z5countIJicEEv1IIXsZT_EE
// MANGLE: _Z4condILi2EEv1IIXqugtT_Li1ELi1ELi2EEE
// MANGLE: _Z4castIiEDTsclfp_ET_
