// [temp.inst]: a recursive member template returning auto&& is not deduced for
// a candidate that is not selected.
template <unsigned long I> struct idx {};
struct Empty {};
struct Node { int head; Empty tail; };
struct Access {
    template <class V>
    static constexpr auto &&get(V &&v, idx<0>) { return v.head; }
    template <class V, unsigned long I>
    static constexpr auto &&get(V &&v, idx<I>) {
        return get(v.tail, idx<I - 1>{});
    }
};

int main() {
    Node n{5, {}};
    return Access::get(n, idx<0>{}) != 5;
}
