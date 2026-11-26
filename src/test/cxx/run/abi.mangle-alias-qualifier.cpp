// [abi:expressions]: Q<args>::template f<args>(), Q an alias template: its name
// and arguments as written (libc++'s optional constructors).
template <bool, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { using type = T; };
template <bool B, class T = void> using enable_if_t = typename enable_if<B, T>::type;
template <bool> struct IfImpl { template <class A, class B> using F = A; };
template <> struct IfImpl<false> { template <class A, class B> using F = B; };
template <bool C, class A, class B> using If = typename IfImpl<C>::template F<A, B>;
struct fail { template <class V> static constexpr bool implicit() { return false; } };
template <class T> struct opt {
    struct Ctor { template <class V> static constexpr bool implicit() { return true; } };
    template <class U> using Check = If<(sizeof(U) > 0), Ctor, fail>;
    template <class U = T, enable_if_t<Check<U>::template implicit<U>(), int> = 0>
    opt(U &&) {}
};

int main() {
    int x = 3;
    opt<int> o(static_cast<int &&>(x));
    return 0;
}
