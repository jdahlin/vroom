// [temp.variadic]/5: a pack expansion in the template arguments of a member
// template named for its address (libc++'s variant dispatch).
struct disp {
    template <class F, class... V> static int get() {
        return sizeof...(V) * 10 + sizeof(F);
    }
};
template <class... Vs> int use() {
    auto p = &disp::template get<char, Vs...>;
    return p();
}
int main() { return use<int, long>() != 21 || use<>() != 1; }
