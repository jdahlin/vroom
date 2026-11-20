/* [temp.mem]/1, [dcl.fct.spec]/2: a constructor template with explicit(bool) of a dependent condition: implicit conversion only for the specializations whose condition is false */
template <class T> struct is_small { static constexpr bool value = sizeof(T) <= 2; };
struct W {
    int v;
    template <class T> explicit(!is_small<T>::value) W(T t) : v(int(t)) {}
};
int take(W w) { return w.v; }
int main()
{
    W a = 'x';
    W b(70000);
    return take('y') - 'y' + a.v - 'x' + b.v - 70000 + take(W(3)) - 3;
}
