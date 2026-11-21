// [temp.alias]/2, [temp.variadic]/4: an alias template forwarding a
// non-type template parameter pack to a class template specialization.
template <unsigned long... Is> struct index_seq {
    static constexpr unsigned long sum = (0UL + ... + Is);
};

template <unsigned long... Is> using alias_seq = index_seq<Is...>;

template <unsigned long... Is> struct holder {
    using type = alias_seq<Is...>;
    static constexpr unsigned long check(alias_seq<Is...>) {
        return type::sum;
    }
};

static_assert(holder<1, 2, 3>::check(index_seq<1, 2, 3>{}) == 6, "");

int main() {
    return holder<4, 5>::check(index_seq<4, 5>{}) != 9;
}
