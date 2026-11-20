/* A function template declared before its definition, after GNU
   attributes (libc++'s _LIBCPP_HIDE_FROM_ABI), its parameter's type naming
   the pack only in a pack expansion (libc++'s get(tuple<_Tp...>&)), and one
   redeclared with a parameter of a class template's specialization
   ([temp.fct]/1, [temp.variadic]/7, [temp.deduct.call]/1). */
#include <stdio.h>

template <class... Ts> struct pack {
    int n() { return sizeof...(Ts); }
};

template <class... Ts>
__attribute__((__visibility__("hidden"))) int size_of(pack<Ts...> &p);

template <class... Ts> int size_of(pack<Ts...> &p) { return p.n() * 10; }

template <class T> struct one {
    T v;
};
template <class T> T value_of(const one<T> &o);
template <class T> T value_of(const one<T> &o) { return o.v + 1; }

/* [temp.arg.explicit]/9: the explicit arguments fill Ts, while F
   and V are still deduced from the call. */
template <class... Ts, class F, class V>
int call_with_pack(F &f, V v) { return sizeof...(Ts) + f(v); }
struct double_value { int operator()(int v) { return 2 * v; } };

int main(void)
{
    pack<int, char> two;
    pack<> none;
    int failures = 0;
    if (size_of(two) != 20)
        failures |= 1;
    if (size_of(none) != 0)
        failures |= 2;
    one<int> o;
    o.v = 6;
    if (value_of(o) != 7)
        failures |= 4;
    double_value double_it;
    if (call_with_pack<int, char, short, long>(double_it, 3) != 10)
        failures |= 8;
    printf("%d\n", failures);
    return failures;
}
