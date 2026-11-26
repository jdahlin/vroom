// [temp.variadic]/5: a pack expansion in the explicit template arguments of a
// member function template (libc++'s variant visitation).
struct base {
    template <class... Ts> static constexpr auto make() { return int(sizeof...(Ts)); }
    template <class... Vs> static int use() {
        constexpr auto m = make<int &&, Vs...>();
        return m;
    }
};
int main() { return base::use<char, long>() != 3; }
