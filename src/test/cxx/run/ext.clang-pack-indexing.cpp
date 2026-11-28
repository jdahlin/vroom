// [ext:clang_pack_indexing] C++26's T...[I] and e...[I], which clang accepts
// in every mode.  libc++'s tuple names _Up...[0] in a member template's
// base-specifier and integer_sequence's get returns _Indices...[_Ip].
template <class... T> struct at {
    using first = T...[0];
    T...[sizeof...(T) - 1] last;
};
template <class T> struct id { using type = T; };
template <unsigned long I, class... T> struct nth { using type = typename id<T...[I]>::type; };
template <class... T> struct box {
    template <class... U> struct inner : id<id<U...[0]>> { using t = id<const U...[0] &>; };
};
template <int... V> int get() { int a = V...[1]; return a + V...[sizeof...(V) - 1]; }
template <class... A> int pick(A... a) {
    decltype(a...[0]) x = a...[0];
    a...[1] += 1;
    return x * 100 + a...[1];
}
template <class... T> void consume(T...[0] x) {}
template <class... T> struct make {
    static int f() { T...[0] v{}; return sizeof(v) + sizeof(typename id<T...[1]>::type); }
};
static_assert(__is_same(at<char, long>::first, char));
static_assert(__is_same(nth<1, int, char>::type, char));
int main() {
    at<int, char> x; x.last = 'a';
    box<int>::inner<short> b;
    if (sizeof x.last != 1) return 1;
    if (get<1, 2, 3>() != 5) return 2;
    if (pick(3, 4) != 305) return 3;
    if (make<short, int>::f() != 6) return 6;
    return 0;
}
