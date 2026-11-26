// [dcl.spec.auto]: substituting a candidate's signature deduces the return
// type of a generic lambda it names (std::function's constructor).
template <class T> T &&declval();
template <class> using void_t = void;
template <class, class... A> struct result {};
template <class... A>
struct result<void_t<decltype(__builtin_invoke(declval<A>()...))>, A...> {
    using type = int;
};
struct I { int n; };
struct W {
    int made;
    template <class F, class = typename result<void, F &, const I &>::type>
    W(F f) : made(1) { f(I{3}); }
};
int seen;
int set(W &&w) { return w.made; }

int main() {
    int r = set([](const auto &i) { seen = i.n; });
    return r != 1 || seen != 3;
}
