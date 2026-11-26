// [abi:mangling-compression]: a pack expansion over the template's own pack
// whose pattern also names the enclosing class's pack, which stays as written
// (_SUBSTPACK_).
template <bool B> struct BC {};
template <bool B> using BoolC = BC<B>;
template <class... P> struct And { static constexpr bool value = true; };
template <class A, class B> struct assignable {};
template <bool, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { using type = T; };
template <class... T> struct tup {
    template <class... U,
              typename enable_if<And<BoolC<sizeof...(T) == sizeof...(U)>,
                                     assignable<T &, U>...>::value,
                                 int>::type = 0>
    int as(tup<U...>) { return sizeof...(U); }
};

int main() {
    tup<int &, int &> a;
    tup<int, int> b;
    return a.as(b) != 2;
}
