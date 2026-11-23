// std: c++20
// [temp.param]/6, [temp.arg.nontype]/1, [dcl.type.decltype]/1.2:
// placeholder non-type template parameters (template <auto V>) and
// decltype(V) of a non-type template parameter.
template <typename T, T P> struct Typed {
    static constexpr T value = P;
};

template <auto V> using Auto = Typed<decltype(V), V>;

template <auto V> struct Constant {
    using type = decltype(V);
    static constexpr type value = V;
};

static_assert(Constant<42>::value == 42);
static_assert(Constant<7L>::value == 7L);
static_assert(sizeof(Constant<7L>::type) == 8);
static_assert(Auto<5>::value == 5);

int main() {
    return Auto<9>::value != 9 || Constant<3>::value != 3;
}
