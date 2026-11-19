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
    printf("%d\n", failures);
    return failures;
}
