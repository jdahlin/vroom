/* [abi:mangle.template-param], [abi:mangle.template-arg],
   [abi:mangling.literal]: instantiated by calls */
template <class T> void f(T) {}
template <class T, class U> T g(U) { return T(); }
template <int N> void value() {}
template <bool B> void flag() {}
template <unsigned long N> void wide() {}
template <class... T> void pack(T...) {}
template <class T> struct box {};
template <template <class> class P> void tt(P<int>) {}
int object;
template <int *P> void address() {}
template <class T> struct outer {
    template <class U> void member(U) {}
};
template <class T> auto ret(T t) -> decltype(t + 1) { return t + 1; }
template <class T> void ref(T &&) {}
void use()
{
    f(1);
    f("s");
    g<int>('c');
    value<3>();
    value<-4>();
    flag<true>();
    wide<5ul>();
    pack(1, 'c');
    pack();
    tt<box>(box<int>());
    address<&object>();
    outer<int>().member('c');
    ret(1);
    ref(object);
}
// MANGLE: _Z1fIiEvT_
// MANGLE: _Z1fIPKcEvT_
// MANGLE: _Z1gIicET_T0_
// MANGLE: _Z5valueILi3EEvv
// MANGLE: _Z5valueILin4EEvv
// MANGLE: _Z4flagILb1EEvv
// MANGLE: _Z4wideILm5EEvv
// MANGLE: _Z4packIJicEEvDpT_
// MANGLE: _Z4packIJEEvDpT_
// MANGLE: _Z2ttI3boxEvT_IiE
// MANGLE: _Z7addressIXadL_Z6objectEEEvv
// MANGLE: _ZN5outerIiE6memberIcEEvT_
// MANGLE: _Z3retIiEDTplfp_Li1EET_
// MANGLE: _Z3refIRiEvOT_
