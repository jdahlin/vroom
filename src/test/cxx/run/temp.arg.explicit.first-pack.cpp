// [temp.arg.explicit]/3 and [temp.variadic]/5: explicit arguments belong to
// the first of several template parameter packs, and a name an inner
// expansion expands is not the outer one's (libc++'s variant dispatch).
template <unsigned long... I> struct seq {};
constexpr int count(int a = 0, int b = 0, int c = 0) { return a + b + c; }
template <class... Vs, unsigned long... J> int two(seq<J...>) {
    return sizeof...(Vs) * 10 + sizeof...(J);
}
template <class... Vs> int one(int j) { return sizeof...(Vs) + j; }
template <class... Vs, unsigned long... J> int each(seq<J...>) {
    return count(one<Vs...>(J)...);
}
int main() {
    if (two<int>(seq<0, 1>{}) != 12)
        return 1;
    return each<char, long>(seq<0, 1>{}) != 5;
}
